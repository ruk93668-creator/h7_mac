import hashlib,json,struct,subprocess,sys
from pathlib import Path
sys.stdout.reconfigure(encoding='utf-8')
here=Path(__file__).resolve().parent;root=here.parent.parent
sys.path.insert(0,str(root/'Tests/OledAsyncAcceptance/python_deps'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile
names=[('初始化和中间横线', '真实Initcall、Device、OLED和应用初始化', 'APPLICATION创建1个100ms周期Timer；仅y=16的128个像素置位'), ('重复初始化', '两次App_Init', '不重复分配Timer'), ('初始化前运行', 'App_Run', '安全返回'), ('100ms边界', '99/100/199/200ms及逐笔完成', '100、200ms触发，两帧共16笔事务'), ('忙时跳过', '第一帧未结束时推进200/300ms', '不重复提交，不记录BUSY为错误，完成后可继续'), ('错误记录', '第一笔完成返回IO后再到下周期', '记录-9，OLED回IDLE，下周期重新启动，保留错误记录'), ('Timer池满', '16个Timer占满后初始化', '返回NO_RESOURCE，无应用运行指针'), ('设备未READY', '只注册后初始化，再完成Device_InitAll后重试', '先返回INVALID_STATE，后成功'), ('设备不存在', 'Registry为空', '返回NOT_FOUND'), ('Ops缺失', 'OLED Ops=NULL', '返回NOT_SUPPORTED，不创建Timer'), ('连续两帧横线字节', '两次刷新并逐字节核对', 'CMD0/DATA0至CMD3/DATA3，横线图案一致，无Page4'), ('App_Run持续轮询', '等待命令时连续20次App_Run，然后完成命令', '20次TakeResult，完成后推进DATA0')]
commands=[]
def run(cmd):
 p=subprocess.run([str(x) for x in cmd],cwd=root,capture_output=True)
 text=(p.stdout+p.stderr).decode('utf-8',errors='replace');commands.append({'command':subprocess.list2cmdline([str(x) for x in cmd]),'code':p.returncode,'output':text})
 (here/'commands.json').write_text(json.dumps(commands,ensure_ascii=False,indent=2),encoding='utf-8')
 print(commands[-1]['command']);print(text,end='')
 if p.returncode:raise RuntimeError('Build failed')
run(['D:/32/ARM/ARMCC/Bin/armcc.exe','--vsn'])
args=['D:/32/ARM/ARMCC/Bin/armcc.exe','--cpu','Cortex-M4','--c99','-O0']
for inc in ['CM4/Device/Inc','CM4/BSP','Common/Inc','Board/Inc','CM4/Application','CM4/Framework','CM4/Platform']:args+=['-I',root/inc]
run(args+['-c',here/'app_test.c','-o',here/'app_test.o'])
run(['D:/32/ARM/ARMCC/Bin/armlink.exe',here/'app_test.o','--entry','App_TestRun','--scatter',here/'test.sct','--keep=*(.framework_init)','--keep=*(.device_export)','--no_scanlib','-o',here/'app_test.axf'])
with (here/'app_test.axf').open('rb') as f:
 e=ELFFile(f);entry=e.header['e_entry'];symbols={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
 sections=[(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
fields=['fail_line','checks','tick','tx_count','takes','active','ready','heap','pool','page','state','refresh_result','process_result','init_result']
rows=[]
for id,(name,inputs,expected) in enumerate(names):
 u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS);u.mem_map(0x08000000,0x100000);u.mem_map(0x20000000,0x40000);u.mem_map(0x01000000,0x1000)
 for a,d in sections:u.mem_write(a,d)
 u.reg_write(UC_ARM_REG_SP,0x2003FFF0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,id)
 error=None
 try:u.emu_start(entry|1,0x01000000,count=10000000,timeout=10000000)
 except Exception as exc:error=str(exc)
 state=dict(zip(fields,struct.unpack('<11I3i',u.mem_read(symbols['g_result'],56))))
 passed=error is None and u.reg_read(UC_ARM_REG_PC)==0x01000000 and u.reg_read(UC_ARM_REG_R0)==0 and state['fail_line']==0
 actual=f"事务{state['tx_count']}笔；Timer池/堆={state['pool']}/{state['heap']}；刷新/轮询结果={state['refresh_result']}/{state['process_result']}；断言{state['checks']}项"
 rows.append(dict(name=name,input=inputs,expected=expected,actual=actual,result='PASS' if passed else 'FAIL',state=state,error=error))
 print(f"{id+1:02} {rows[-1]['result']}: {name}: {actual}; fail_line={state['fail_line']}; error={error}")
summary=dict(total=len(rows),passed=sum(r['result']=='PASS' for r in rows),failed=sum(r['result']=='FAIL' for r in rows),assertions=sum(r['state']['checks'] for r in rows))
(here/'results.json').write_text(json.dumps(dict(summary=summary,cases=rows),ensure_ascii=False,indent=2),encoding='utf-8')
lines=['# CM4 OLED Application集成模拟测试','','真实Application、Timer、Event、Runtime、Device、OLED和Initcall源码；仅模拟BSP通信和Platform_TimeGetMs。','', '| 测试 | 输入 | 预期 | 实际 | 结果 |','|---|---|---|---|---|']
lines += [f"| {r['name']} | {r['input']} | {r['expected']} | {r['actual']} | {r['result']} |" for r in rows]
lines += ['',str(summary),'','不执行真实I2C电气行为或真实BSP/HAL，不能代替上板测试。Application通过真实OLED Ops清屏并绘制y=16整条横线，测试核对真实帧缓冲与发送字节。']
(here/'results.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print('TOTAL',summary)
raise SystemExit(0 if not summary['failed'] else 1)
