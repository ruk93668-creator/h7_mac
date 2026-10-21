import argparse,hashlib,json,struct,subprocess,sys,tempfile
from pathlib import Path

parser=argparse.ArgumentParser();parser.add_argument('--deps',type=Path);args=parser.parse_args()
sys.stdout.reconfigure(encoding='utf-8')
here=Path(__file__).resolve().parent;root=here.parent.parent
if args.deps is None:
    args.deps=here/'python_deps'
sys.path.insert(0,str(args.deps))
import unicorn
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_MEM_READ,UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile

cases=[
 ('正常整帧','调用Refresh，依次完成8笔传输，每笔后调用Process','CMD0→DATA0→CMD1→DATA1→CMD2→DATA2→CMD3→DATA3；命令和512字节数据全部匹配，回IDLE'),
 ('命令未完成','Refresh成功后连续Process 12次，不交付结果','每次SYS_OK，WAIT_CMD、页号、事务数量不变'),
 ('数据未完成','CMD0完成并启动DATA0，连续Process 12次，不交付结果','每次SYS_OK，WAIT_DATA、页号、事务数量不变'),
 ('命令期间重复Refresh','WAIT_CMD时再次Refresh','BUSY=-5；状态、页号不变，不调用BSP提交'),
 ('数据期间重复Refresh','WAIT_DATA时再次Refresh','BUSY=-5；状态、页号不变，不调用BSP提交'),
 ('最后一页边界','完成DATA3，然后连续Process 10次','回IDLE，页号保持3；只有8笔事务，不产生CMD4，不再取BSP结果'),
 ('连续两帧','完成第一帧后更换全部帧数据，重新Refresh并完成第二帧','第二帧从CMD0启动，共16笔事务，第二帧使用新数据，两帧分别回IDLE'),
]
stage_names=[f'{kind}{page}' for page in range(4) for kind in ['CMD','DATA']]
for label,code in [('IO',-9),('TIMEOUT',-8)]:
    for stage in stage_names:
        cases.append((f'{stage}完成返回{label}',f'正常推进到{stage}，完成结果为{code}',f'Process原样返回{code}，状态回IDLE，不发送下一笔'))
for label,code in [('BUSY',-5),('IO',-9),('TIMEOUT',-8)]:
    for stage in stage_names:
        cases.append((f'{stage}提交返回{label}',f'第{stage_names.index(stage)+1}笔提交返回{code}',f'Refresh或Process原样返回{code}，回IDLE，无额外传输'))
cases += [
 ('空指针','Refresh(NULL)、Process(NULL)','均返回INVALID_ARG=-1，无BSP调用'),
 ('无效状态','refresh_state=99，Process','返回INVALID_STATE=-4，无BSP调用'),
 ('通信错误后重新刷新','DATA1完成返回IO，然后发起完整新帧','错误原样上报；随后从CMD0重启并完成整帧'),
]
assert len(cases)==50
commands=[]
def run(cmd):
    p=subprocess.run([str(x) for x in cmd],cwd=root,capture_output=True)
    text=(p.stdout+p.stderr).decode('utf-8',errors='replace')
    commands.append({'command':subprocess.list2cmdline([str(x) for x in cmd]),'exit_code':p.returncode,'output':text})
    (here/'commands.json').write_text(json.dumps(commands,ensure_ascii=False,indent=2),encoding='utf-8')
    print(commands[-1]['command']);print(text,end='')
    if p.returncode:raise RuntimeError('Build failed')
    return text
bin=Path('D:/32/ARM/ARMCC/Bin');obj=here/'oled_async_test.o';image=here/'oled_async_test.axf'
version=run([bin/'armcc.exe','--vsn'])
run([bin/'armcc.exe','--cpu','Cortex-M4','--c99','-O0','-I',root/'CM4/Device/Inc','-I',root/'CM4/BSP','-I',root/'Common/Inc','-I',root/'Board/Inc','-c',here/'oled_async_test.c','-o',obj])
run([bin/'armlink.exe',obj,'--entry','Oled_TestRun','--scatter',here/'test.sct','--keep=*(.device_export)','--no_scanlib','-o',image])
with image.open('rb') as f:
    elf=ELFFile(f);entry=elf.header['e_entry'];symbols={s.name:s for s in elf.get_section_by_name('.symtab').iter_symbols()}
    sections=[(s['sh_addr'],s.data()) for s in elf.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
    addresses={n:s['st_value'] for n,s in symbols.items()}
    guard_base=addresses['g_guarded'];guard_end=guard_base+symbols['g_guarded']['st_size']
fields=['fail_line','checks','tx_count','attempts','takes','active','ready','overlap','step_count','state','page','frame_offset','oled_size','frame_size','tx_size','last_result']
rows=[];states=['IDLE','WAIT_CMD','WAIT_DATA'];test_lines=(here/'oled_async_test.c').read_text().splitlines()
for id,(name,inputs,expected) in enumerate(cases):
    uc=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
    uc.mem_map(0x08000000,0x100000);uc.mem_map(0x20000000,0x40000);uc.mem_map(0x01000000,0x1000)
    for a,d in sections:uc.mem_write(a,d)
    uc.mem_write(guard_base,b'\xA5'*16);uc.mem_write(guard_end-16,b'\x5A'*16)
    violations=[]
    def guard_hook(u,access,addr,size,value,data):
        violations.append({'address':hex(addr),'size':size,'pc':hex(u.reg_read(UC_ARM_REG_PC)),'access':access})
    uc.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,guard_hook,begin=guard_base,end=guard_base+15)
    uc.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,guard_hook,begin=guard_end-16,end=guard_end-1)
    uc.reg_write(UC_ARM_REG_SP,0x2003FFF0);uc.reg_write(UC_ARM_REG_LR,0x01000001);uc.reg_write(UC_ARM_REG_R0,id)
    error=None
    try:uc.emu_start(entry|1,0x01000000,timeout=10000000,count=10000000)
    except Exception as exc:error=str(exc)
    state=dict(zip(fields,struct.unpack('<15Ii',uc.mem_read(addresses['g_result'],64))))
    trace=[dict(zip(['kind','state','page','tx_count','result'],struct.unpack('<4Ii',uc.mem_read(addresses['g_steps']+i*20,20)))) for i in range(min(state['step_count'],256))]
    transactions=[];current_page=0
    for i in range(min(state['tx_count'],32)):
        ptr=addresses['g_tx']+i*state['tx_size'];length,addr,result=struct.unpack('<IIi',uc.mem_read(ptr,12))
        data=bytes(uc.mem_read(ptr+12,min(length,129)))
        if length==4 and len(data)>1:
            current_page=data[1]&15;label=f'CMD{current_page}'
        else:label=f'DATA{current_page}'
        transactions.append({'stage':label,'length':length,'address':addr,'bytes':data.hex(' '),'result':result})
    canaries=bytes(uc.mem_read(guard_base,16))==b'\xA5'*16 and bytes(uc.mem_read(guard_end-16,16))==b'\x5A'*16
    passed=error is None and uc.reg_read(UC_ARM_REG_PC)==0x01000000 and uc.reg_read(UC_ARM_REG_R0)==0 and state['fail_line']==0 and not violations and canaries and not state['overlap']
    errors=[step['result'] for step in trace if step['kind'] in (1,2) and step['result']<0]
    actual=f"最终记录返回{state['last_result']}；错误返回{errors}；状态{states[state['state']] if state['state']<3 else state['state']}；页{state['page']}；事务{state['tx_count']}笔；断言{state['checks']}；越界监控{len(violations)}次"
    if id==47:actual='Refresh(NULL)、Process(NULL)均断言返回-1；'+actual
    if not passed:actual+=f"；失败行{state['fail_line']}；异常{error}"
    row={'id':id+1,'name':name,'input':inputs,'expected':expected,'actual':actual,'result':'PASS' if passed else 'FAIL','state':state,'trace':trace,'transactions':transactions,'guard_violations':violations,'canaries_intact':canaries,'emulator_error':error,
         'failing_test_line':test_lines[state['fail_line']-1] if state['fail_line'] and state['fail_line']<=len(test_lines) else None}
    rows.append(row);print(f"{id+1:02d} {row['result']}: {name}: {actual}")
summary={'total':len(rows),'pass':sum(r['result']=='PASS' for r in rows),'fail':sum(r['result']=='FAIL' for r in rows),'not_run':0,'assertions':sum(r['state']['checks'] for r in rows),'compiler':version,'unicorn':unicorn.__version__}
(here/'results.json').write_text(json.dumps({'summary':summary,'cases':rows},ensure_ascii=False,indent=2),encoding='utf-8')
lines=['# CM4 OLED异步刷新模拟验收','', '真实源码 + Mock BSP；ARMCC5编译ARM可执行程序，Unicorn实际运行。','', '| 编号 | 用例 | 输入 | 预期 | 实际 | 结果 |','|---|---|---|---|---|---|']
lines += [f"| {r['id']} | {r['name']} | {r['input']} | {r['expected']} | {r['actual']} | {r['result']} |" for r in rows]
lines += ['',f"合计 {summary['total']}，PASS {summary['pass']}，FAIL {summary['fail']}，未执行0；断言{summary['assertions']}项。",'',
          '正常第一帧实际事务：'+' → '.join(t['stage'] for t in rows[0]['transactions']), '',
          '完整命令和像素字节、逐步状态记录见results.json。测试不模拟SSD1306电气行为；Mock按当前BSP契约在WriteAsync返回前复制数据，并保持单笔事务，结果消费后才可再次提交。',
          '对象前后16字节监控读写并检查哨兵；全部正常页数据逐字节比较。未向非法page值强行注入内存损坏，本报告的越界结论针对有效刷新状态链。','', '## 实际编译命令','', '```text']
lines += [c['command'] for c in commands];lines += ['```']
(here/'results.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
baseline=json.loads((here/'source_before.json').read_text())
changed=[p for p,h in baseline.items() if not (root/p).exists() or hashlib.sha256((root/p).read_bytes()).hexdigest()!=h]
(here/'source_integrity.json').write_text(json.dumps({'checked':len(baseline),'changed':changed,'oled_c_sha256':hashlib.sha256((root/'CM4/Device/Src/oled_device.c').read_bytes()).hexdigest()},indent=2))
print(f"TOTAL {summary['total']} PASS {summary['pass']} FAIL {summary['fail']} NOT_RUN 0; assertions={summary['assertions']}; source_changes={len(changed)}")
raise SystemExit(0 if summary['fail']==0 and not changed else 1)
