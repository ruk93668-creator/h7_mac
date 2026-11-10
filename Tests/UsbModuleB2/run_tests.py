"""Execute ARMCC5-built baseline/current production sources; compare complete call traces."""
from pathlib import Path
import subprocess, sys, struct, json, hashlib
ROOT=Path(__file__).resolve().parents[2]; HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/UsbModuleB2'; OUT.mkdir(parents=True,exist_ok=True)
BASE='8bfa9bc4316c0faaceef65bc90fbe84d03eae55f'
sys.path.insert(0,str(ROOT/'Tests/OledAsyncAcceptance/python_deps'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile
commands=[]
def run(cmd):
 p=subprocess.run(list(map(str,cmd)),cwd=ROOT,capture_output=True)
 log=(p.stdout+p.stderr).decode('utf-8',errors='replace')
 commands.append({'command':list(map(str,cmd)),'exit':p.returncode,'output':log})
 (OUT/'commands.json').write_text(json.dumps(commands,indent=2),encoding='utf-8')
 if p.returncode:raise RuntimeError(log)
 return log
baseline=OUT/'baseline'; baseline.mkdir(exist_ok=True)
rel='CM7/Application/Tasks/usb_task.c'
raw=subprocess.check_output(['git','-c','safe.directory='+ROOT.as_posix(),'show',BASE+':'+rel],cwd=ROOT)
(baseline/'usb_task.c').write_bytes(raw)
cc=['D:/32/ARM/ARMCC/Bin/armcc.exe','--cpu','Cortex-M7.fp.sp','--c99','-O0','-DSTM32H747xx','-DCORE_CM7','-DUSE_HAL_DRIVER','--preinclude',ROOT/'CM7/BSP/bsp_usb_hal_config.h']
for inc in ['CM7/Core/Inc','CM7/Device/Inc','Board','Board/Inc','Middlewares/Third_Party/FreeRTOS/Source/include','Middlewares/Third_Party/FreeRTOS/Source/portable/RVDS/ARM_CM7/r0p1','Drivers/STM32H7xx_HAL_Driver/Inc','Drivers/CMSIS/Include','Drivers/CMSIS/Device/ST/STM32H7xx/Include','CM7/Application','CM7/Application/Tasks','Common/Inc','CM7/Media/Inc','CM7/BSP','CM7/Platform','CM7/Service','CM7/Framework','Middlewares/ST/STM32_USB_Device_Library/Core/Inc']:
 cc+=['-I',ROOT/inc]
sc=OUT/'test.sct';sc.write_text('LR_TEST 0x08000000 0x100000 { ER_TEST 0x08000000 { .ANY (+RO) } RW_TEST 0x20000000 0x40000 { .ANY (+RW +ZI) } }\n')
images={}
for name,tp in [('baseline',baseline/'usb_task.c'),('current',ROOT/'CM7/Application/Tasks/usb_task.c')]:
 obj=OUT/(name+'.o');axf=OUT/(name+'.axf')
 defs=['-DB2_TASK_SOURCE="'+tp.as_posix()+'"']
 if name=='baseline':defs+=['-DB2_BASELINE']
 else:defs+=['-DB2_APP_SOURCE="'+(ROOT/'CM7/Application/app_usb.c').as_posix()+'"']
 run(cc+defs+['-c',HERE/'behavior_test.c','-o',obj])
 run(['D:/32/ARM/ARMCC/Bin/armlink.exe',obj,'--entry','B2_TestRun','--scatter',sc,'--no_scanlib','-o',axf])
 with axf.open('rb') as f:
  e=ELFFile(f);syms={x.name:x['st_value'] for x in e.get_section_by_name('.symtab').iter_symbols()}
  sections=[(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
 images[name]=(syms,sections)
def execute(name,id,entry='B2_TestRun'):
 syms,sections=images[name];u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
 for a,n in [(0x08000000,0x100000),(0x20000000,0x40000),(0x01000000,0x1000)]:u.mem_map(a,n)
 for a,d in sections:u.mem_write(a,d)
 u.reg_write(UC_ARM_REG_SP,0x2003fff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,id)
 stopped=[]
 if id%32==27 and entry=='B2_TestRun':
  def hook(cpu,addr,size,_):
   if addr==(syms['App_EventsWait']&~1) and struct.unpack('<I',cpu.mem_read(syms['waits'],4))[0]==7:
    stopped.append(True);cpu.emu_stop()
  u.hook_add(UC_HOOK_CODE,hook)
 error=None
 try:u.emu_start(syms[entry]|1,0x01000000,count=10000000)
 except Exception as ex:error=str(ex)
 count=struct.unpack('<I',u.mem_read(syms['g_trace_count'],4))[0]
 if count>1024:raise RuntimeError((name,id,count,error))
 data=bytes(u.mem_read(syms['g_trace'],count*120)) if count else b''
 trace=[list(x) for x in struct.iter_unpack('<30I',data)]
 failure=struct.unpack('<I',u.mem_read(syms['g_failure'],4))[0]
 ok=not error and not failure and (bool(stopped) or (u.reg_read(UC_ARM_REG_PC)==0x01000000 and u.reg_read(UC_ARM_REG_R0)==0))
 return {'ok':ok,'error':error,'failure':failure,'return':u.reg_read(UC_ARM_REG_R0),'trace':trace,'trace_sha256':hashlib.sha256(data).hexdigest()}
names=['BOOT gate','Owner gate','STOP before START','Unknown command','START IO failure','Not initialized','ACK failure with FAULT','Configured after START','Repeated START BUSY fault','Mode error same-round cancel','Abort failure ownership','Bridge Return ACK wait','Mode Stop IO retry','Mode Stop BUSY retry','Stream query failure','Class absent on STOP','EOF FPS sample','Tick and EOF wrap','FPS overflow guard','Task wait error retained','SOF coalescing','SOF wrap','ACK and FAULT independent retry','STOP START FIFO','Repeated STOP','EOF event observation (stub)','Frame loan then STOP','Wait and stack cadence','Task stack and priority','Duplicate task create','Task allocation failure','Fault report reset on START']
if __name__=='__main__':
 rows=[]
 for id in range(96):
  old=execute('baseline',id);new=execute('current',id)
  same=old['trace']==new['trace'];passed=old['ok'] and new['ok'] and same
  row={'id':id,'mode':['UVC','TEST','NONE'][id//32],'name':names[id%32],'pass':passed,'baseline_ok':old['ok'],'current_ok':new['ok'],'trace_equal':same,'trace_records':len(new['trace']),'trace_sha256':new['trace_sha256']}
  if not passed:
   row['old']={k:v for k,v in old.items() if k!='trace'};row['new']={k:v for k,v in new.items() if k!='trace'}
   for i,(a,b) in enumerate(zip(old['trace'],new['trace'])):
    if a!=b:row['first_difference']={'record':i,'baseline':a,'current':b};break
  (OUT/(f'trace_{id:02d}.json')).write_text(json.dumps({'baseline':old,'current':new},indent=2),encoding='utf-8')
  rows.append(row);print(id,row['mode'],row['name'],'PASS' if passed else 'FAIL',flush=True)
 for id,title in enumerate(['NULL command','NULL reply','Pure getters and ACK confirmation','FAULT confirmation preserves ACK','ACK confirmation preserves FAULT','Diagnostic observer is optional']):
  x=execute('current',id,'B2_InterfaceRun')
  rows.append({'id':96+id,'name':title,'pass':x['ok'],'failure_line':x['return'],'error':x['error']})
  print(96+id,title,'PASS' if x['ok'] else 'FAIL',x['return'],flush=True)
 summary={'total':len(rows),'passed':sum(x['pass'] for x in rows),'failed':sum(not x['pass'] for x in rows)}
 (HERE/'results.json').write_text(json.dumps({'baseline':BASE,'summary':summary,'cases':rows,'hardware_verified':False,'trace_fields':['call','arg1','arg2','tick','Debug[12]','started','stopping','stop_command','ack_pending','fault','fault_reported','fps_tick','fps_frames','Mode state','frame_owned','Bridge idle','UVC state','EOF count','SOF sequence']},indent=2),encoding='utf-8')
 print(summary);raise SystemExit(bool(summary['failed']))
