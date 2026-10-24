import json,struct,subprocess,sys,hashlib
from pathlib import Path
sys.stdout.reconfigure(encoding='utf-8')
here=Path(__file__).resolve().parent;root=here.parents[1]
sys.path.insert(0,str(root/'Tests/OledAsyncAcceptance/python_deps'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile
commands=[]
source_before={"CM7/Media/Src/camera_capture.c":hashlib.sha256((root/"CM7/Media/Src/camera_capture.c").read_bytes()).hexdigest()}
def run(cmd):
 p=subprocess.run(list(map(str,cmd)),cwd=root,capture_output=True)
 output=(p.stdout+p.stderr).decode('utf-8',errors='replace')
 commands.append({'command':subprocess.list2cmdline(list(map(str,cmd))),'returncode':p.returncode,'output':output})
 (here/'commands.json').write_text(json.dumps(commands,ensure_ascii=False,indent=2),encoding='utf-8')
 if p.returncode:print(output);raise RuntimeError('compile/link failed')
run(['D:/32/ARM/ARMCC/Bin/armcc.exe','--vsn'])
args=['D:/32/ARM/ARMCC/Bin/armcc.exe','--cpu','Cortex-M7','--c99','-O0']
for inc in [root/'Tests/CameraStreamState/stubs',root/'Common/Inc',root/'CM7/Media/Inc',root/'CM7/BSP',root/'CM7/Platform']:args+=['-I',inc]
run(args+['-c',here/'context_test.c','-o',here/'stream_test.o'])
run(['D:/32/ARM/ARMCC/Bin/armlink.exe',here/'stream_test.o','--entry','Stream_TestRun','--scatter',here/'test.sct','--no_scanlib','-o',here/'stream_test.axf'])
with (here/'stream_test.axf').open('rb') as f:
 e=ELFFile(f);entry=e.header['e_entry'];syms={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
 sections=[(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
groups=[('结构与初始化', '结构与初始化', '状态与返回值符合约定'), ('Snapshot释放后启动Stream', 'Snapshot释放后启动Stream', '状态与返回值符合约定'), ('Stream停止后启动Snapshot', 'Stream停止后启动Snapshot', '状态与返回值符合约定'), ('Snapshot停止后启动Stream', 'Snapshot停止后启动Stream', '状态与返回值符合约定'), ('Stream启动IO错误', 'Stream启动IO错误', '状态与返回值符合约定'), ('Stop失败保留Stream与首次错误', 'Stop失败保留Stream与首次错误', '状态与返回值符合约定'), ('Stream不使用Snapshot超时', 'Stream不使用Snapshot超时', '状态与返回值符合约定'), ('Stream不处理Snapshot FRAME', 'Stream不处理Snapshot FRAME', '状态与返回值符合约定'), ('Stream错误恢复', 'Stream错误恢复', '状态与返回值符合约定'), ('Stream错误且恢复重试', 'Stream错误且恢复重试', '状态与返回值符合约定'), ('嵌套回调上下文验证', '嵌套回调上下文验证', '状态与返回值符合约定'), ('Snapshot完整Get Release及再次启动', 'Snapshot完整Get Release及再次启动', '状态与返回值符合约定'), ('Snapshot失败恢复保留模式', 'Snapshot失败恢复保留模式', '状态与返回值符合约定'), ('READY拒绝Stream和Stop', 'READY拒绝Stream和Stop', '状态与返回值符合约定'), ('Stream拒绝Snapshot及重复启动', 'Stream拒绝Snapshot及重复启动', '状态与返回值符合约定'), ('空闲Stop无硬件调用', '空闲Stop无硬件调用', '状态与返回值符合约定'), ('非法配置不初始化', '非法配置不初始化', '状态与返回值符合约定'), ('Stream初始化失败不占模式', 'Stream初始化失败不占模式', '状态与返回值符合约定'), ('Stream启动BUSY恢复', 'Stream启动BUSY恢复', '状态与返回值符合约定'), ('Stream启动TIMEOUT恢复', 'Stream启动TIMEOUT恢复', '状态与返回值符合约定'), ('Rotate失败不提交对象', 'Rotate失败不提交对象', '状态与返回值符合约定'), ('Snapshot Tick回绕99ms', 'Snapshot Tick回绕99ms', '状态与返回值符合约定'), ('Snapshot Tick回绕100ms', 'Snapshot Tick回绕100ms', '状态与返回值符合约定'), ('Stream临界区保持PRIMASK', 'Stream临界区保持PRIMASK', '状态与返回值符合约定')]
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
