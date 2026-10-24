import json,struct,subprocess,sys,hashlib
from pathlib import Path
sys.stdout.reconfigure(encoding='utf-8')
here=Path(__file__).resolve().parent;root=here.parents[1]
sys.path.insert(0,str(root/'Tests/OledAsyncAcceptance/python_deps'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile
commands=[]
source_before={f:hashlib.sha256((root/f).read_bytes()).hexdigest() for f in json.loads((here/"source_baseline.json").read_text()).keys()}
def run(cmd):
 p=subprocess.run(list(map(str,cmd)),cwd=root,capture_output=True)
 output=(p.stdout+p.stderr).decode('utf-8',errors='replace')
 commands.append({'command':subprocess.list2cmdline(list(map(str,cmd))),'returncode':p.returncode,'output':output})
 (here/'commands.json').write_text(json.dumps(commands,ensure_ascii=False,indent=2),encoding='utf-8')
 if p.returncode:print(output);raise RuntimeError('compile/link failed')
run(['D:/32/ARM/ARMCC/Bin/armcc.exe','--vsn'])
args=['D:/32/ARM/ARMCC/Bin/armcc.exe','--cpu','Cortex-M7','--c99','-O0']
for inc in [here/'stubs',root/'Common/Inc',root/'CM7/Media/Inc',root/'CM7/BSP',root/'CM7/Platform']:args+=['-I',inc]
run(args+['-c',here/'stream_test.c','-o',here/'stream_test.o'])
run(['D:/32/ARM/ARMCC/Bin/armlink.exe',here/'stream_test.o','--entry','Stream_TestRun','--scatter',here/'test.sct','--no_scanlib','-o',here/'stream_test.axf'])
with (here/'stream_test.axf').open('rb') as f:
 e=ELFFile(f);entry=e.header['e_entry'];syms={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
 sections=[(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
groups=[
('三缓冲初始化地址容量及对齐','160x120配置','DMA索引0/1，第三块FREE，无消费者，物理范围不超过128KB'),
('空队列查找及获取释放拒绝','刚初始化','Find返回2，Get为false，Release返回INVALID_STATE'),
('M0首次完成及获取释放','完成M0','替换M0为Buffer2，Buffer0交付，重复Get/Release被拒绝'),
('M1首次完成及获取释放','完成M1','替换M1为Buffer2，Buffer1交付，重复Get/Release被拒绝'),
('旧READY持续覆盖','交替完成30帧，不获取','保留最新帧30，丢帧29，映射与硬件地址一致'),
('Application长时间持有','获取第一帧后1000次DMA完成','持有帧字节不变，不切换IN_USE，丢帧1000，释放后可重新交付'),
('FREE目标切换失败','切换回调依次返回IO/BUSY/TIMEOUT','原错误返回，整个软件对象及硬件目标不变，成功重试可提交'),
('READY目标切换失败','有旧READY时切换失败','旧帧仍保留，计数和软件对象不变，成功重试才回收'),
('初始化非法参数','NULL、奇数宽、未知格式','返回INVALID_ARG，已有对象不变'),
('完成回调非法参数','NULL对象/回调、非法槽位','返回INVALID_ARG，无副作用'),
('DMA索引越界','M0索引3','返回INVALID_STATE，无地址切换'),
('DMA目标重叠','M0/M1同一缓冲区','返回INVALID_STATE，无地址切换'),
('DMA所有权错误','目标为READY','返回INVALID_STATE，无地址切换'),
('消费者索引越界','consumer索引3','返回INVALID_STATE，无地址切换'),
('消费者与DMA目标冲突','consumer等于M0','返回INVALID_STATE，无地址切换'),
('消费者指向FREE','第三块FREE且consumer关联','返回INVALID_STATE，无地址切换'),
('READY缺少消费者关联','第三块READY、consumer无效','返回INVALID_STATE，无地址切换'),
('替换地址容量保护','NULL地址、零长度、超容量、非Word长度','返回INVALID_STATE，软件对象不变，无地址切换'),
('获取释放NULL参数','NULL对象/配置/输出','Get为false、Release为INVALID_ARG，输出不变'),
('PRIMASK原值0恢复','Get/Release成功与失败','出口恢复0'),
('PRIMASK原值1恢复','Get/Release成功与失败','出口恢复1'),
('Get临界区完成中断交错','屏蔽后排队M1完成','退出临界区后才执行，已获取帧不被覆盖'),
('Release临界区完成中断交错','屏蔽后排队M1完成','先释放再处理完成，重新产生READY'),
('原先屏蔽时中断持续排队','初始PRIMASK=1，Get期间完成','Get后仍不执行，显式恢复0才处理完成'),
('独立模型随机状态机','固定种子3000步：完成/获取/释放/切换失败','每步与独立模型一致，DMA始终拥有两块且不覆盖IN_USE')]
rows=[]
for kind,(name,input_,expected) in enumerate(groups):
 for fmt,label in enumerate(['Y8','RGB565','YUV422']):
  id=kind*3+fmt;u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
  for a,n in [(0x08000000,0x100000),(0x20000000,0x40000),(0xD0000000,0x20000),(0x01000000,0x1000)]:u.mem_map(a,n)
  for a,d in sections:u.mem_write(a,d)
  u.reg_write(UC_ARM_REG_SP,0x2003FFF0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,id)
  error=None
  try:u.emu_start(entry|1,0x01000000,count=1500000000,timeout=45000000)
  except Exception as exc:error=str(exc)
  state=dict(zip(['fail_line','checks','operations','replaces','deferred','case_id'],struct.unpack('<6I',u.mem_read(syms['g_result'],24))))
  passed=not error and u.reg_read(UC_ARM_REG_PC)==0x01000000 and u.reg_read(UC_ARM_REG_R0)==0 and not state['fail_line']
  rows.append({'id':id,'name':name+' / '+label,'input':input_,'expected':expected,'result':'PASS' if passed else 'FAIL','actual':state,'error':error})
  print(f"{id+1:02d} {rows[-1]['result']} {name}/{label}: {state}; {error}",flush=True)
  (here/'results.json').write_text(json.dumps({'cases':rows},ensure_ascii=False,indent=2),encoding='utf-8')
summary={'total':len(rows),'passed':sum(r['result']=='PASS' for r in rows),'failed':sum(r['result']=='FAIL' for r in rows),'assertions':sum(r['actual']['checks'] for r in rows)}
baseline=source_before;changed=[f for f,h in baseline.items() if hashlib.sha256((root/f).read_bytes()).hexdigest()!=h]
(here/'results.json').write_text(json.dumps({'summary':summary,'formal_source_changed':changed,'cases':rows},ensure_ascii=False,indent=2),encoding='utf-8')
lines=['# CameraStream隔离状态机测试','','真实camera_capture.c直接编译到测试翻译单元；不链接HAL。测试在Unicorn执行ARMCC5生成的ARM机器码。','', '| 用例 | 输入 | 预期 | 实际 | 结果 |','|---|---|---|---|---|']
for row in rows:lines.append('| '+row['name']+' | '+row['input']+' | '+row['expected']+' | '+str(row['actual'])+' | '+row['result']+' |')
lines+=['',str(summary),'','正式源文件变化：'+str(changed),'','限制：不验证真实DMA双缓冲CT/中断时序、BSP切换实现、传感器信号、MPU或像素格式正确性。模拟DMA写入仅为测试数据。']
(here/'results.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print('TOTAL',summary,'formal source changed:',changed,flush=True)
raise SystemExit(bool(summary['failed'] or changed))
