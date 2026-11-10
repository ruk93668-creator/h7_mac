"""Compare baseline/current ARMCC5 production Storage, Task, JPEG and AVI traces."""
from pathlib import Path
import subprocess,sys,struct,json,hashlib
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/StorageModuleB3';OUT.mkdir(parents=True,exist_ok=True)
BASE='d725a698203b3bcde479a290714a75b16bc9de16'
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
cc=['D:/32/ARM/ARMCC/Bin/armcc.exe','--cpu','Cortex-M7.fp.sp','--c99','-O0','-DSTM32H747xx','-DCORE_CM7','-DUSE_HAL_DRIVER','--preinclude',ROOT/'CM7/BSP/bsp_usb_hal_config.h']
for inc in ['CM7/Core/Inc','CM7/Device/Inc','Board','Board/Inc','Middlewares/Third_Party/FreeRTOS/Source/include','Middlewares/Third_Party/FreeRTOS/Source/portable/RVDS/ARM_CM7/r0p1','Drivers/STM32H7xx_HAL_Driver/Inc','Drivers/CMSIS/Include','Drivers/CMSIS/Device/ST/STM32H7xx/Include','CM7/Application','CM7/Application/Tasks','Common/Inc','CM7/Media/Inc','CM7/BSP','CM7/Platform','CM7/Service','CM7/Framework','Middlewares/ST/STM32_USB_Device_Library/Core/Inc']:
 cc+=['-I',ROOT/inc]

cc+=['-I',OUT]
sc=OUT/'test.sct';sc.write_text('LR_TEST 0x08000000 0x100000 { ER_TEST 0x08000000 { .ANY (+RO) } RW_TEST 0x20000000 0x100000 { .ANY (+RW +ZI) } }\n')
jpeg=(ROOT/'Tests/CameraJpegStage03A/Hardware/VerifiedCapture/jpeg_complete.jpg').read_bytes()
(OUT/'jpeg_data.h').write_text('static const unsigned char jpeg_data[] = {'+','.join(map(str,jpeg))+'};\n')
images={}
for name in ['baseline','current']:
 dest=OUT/name;dest.mkdir(exist_ok=True)
 sources=[]
 for rel in ['CM7/Application/app_storage.c','CM7/Application/Tasks/storage_task.c']:
  source=ROOT/rel
  if name=='baseline':
   source=dest/Path(rel).name
   source.write_bytes(subprocess.check_output(['git','-c','safe.directory='+ROOT.as_posix(),'show',BASE+':'+rel],cwd=ROOT))
  sources.append(source)
 defs=['-DB3_STORAGE_SOURCE="'+sources[0].as_posix()+'"','-DB3_TASK_SOURCE="'+sources[1].as_posix()+'"']
 if name=='baseline':defs+=['-DB3_BASELINE']
 obj=dest/'behavior.o';axf=dest/'behavior.axf'
 run(cc+defs+['-c',HERE/'behavior_test.c','-o',obj])
 run(['D:/32/ARM/ARMCC/Bin/armlink.exe',obj,'--entry','B3_TestRun','--scatter',sc,'-o',axf])
 with axf.open('rb') as f:
  e=ELFFile(f);syms={x.name:x['st_value'] for x in e.get_section_by_name('.symtab').iter_symbols()}
  sections=[(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
 images[name]=(syms,sections)
def execute(name,id,entry='B3_TestRun'):
 syms,sections=images[name];u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
 for a,n in [(0x08000000,0x100000),(0x20000000,0x100000),(0xD0000000,0x600000),(0x01000000,0x1000)]:u.mem_map(a,n)
 for a,d in sections:u.mem_write(a,d)
 u.reg_write(UC_ARM_REG_SP,0x200ffff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,id)
 stopped=[]
 if entry=='B3_TaskRun' and id>=2:
  def hook(cpu,addr,size,_):
   if addr==(syms['App_EventsWait']&~1) and struct.unpack('<I',cpu.mem_read(syms['waits'],4))[0]==6:stopped.append(True);cpu.emu_stop()
  u.hook_add(UC_HOOK_CODE,hook)
 error=None
 try:u.emu_start(syms[entry]|1,0x01000000,count=30000000)
 except Exception as ex:error=str(ex)
 count=struct.unpack('<I',u.mem_read(syms['g_trace_count'],4))[0]
 data=bytes(u.mem_read(syms['g_trace'],min(count,1024)*384))
 trace=[list(x) for x in struct.iter_unpack('<96I',data)]
 failure=struct.unpack('<I',u.mem_read(syms['g_failure'],4))[0]
 result=u.reg_read(UC_ARM_REG_R0)
 ok=not error and not failure and (bool(stopped) or (u.reg_read(UC_ARM_REG_PC)==0x01000000 and result==0))
 return {'ok':ok,'error':error,'failure':failure,'return':result,'trace':trace,'trace_sha256':hashlib.sha256(data).hexdigest()}
names=['NULL prepare','Invalid config','Unprepared request','Duplicate prepare','Command queue full','Non JPEG rejection','TEST rejection','Context and NULL status','Mount failure recovery','Space query failure recovery','Recover outside FAULT','STOP before start','Four request FIFO','Snapshot complete','Two-span snapshot','Readback corruption','Read failure','Snapshot close failure','Two-span JPEG CRC','No-frame timeout','Repeated START','STOP admission gate','Generation exhausted','Retain failure','Occupied slot','JPEG invalid','Open failure','AVI partial failure','Short write','Periodic sync failure','Old session','Old generation','Queued frame then STOP','Release failure retry','Wrong owner return','Repeated return','Cached status timing','Tick wrap','AVI Finish write failure','AVI Close failure','Unmount failure','AVI Header seek failure','Repeated STOP']
if __name__=='__main__':
 rows=[]
 for entry,cases in [('B3_TestRun',list(enumerate(names))),('B3_TaskRun',list(enumerate(['Static task create','Allocation failure','BOOT wait and stack cadence','Prepare failure gate'])))]:
  for id,title in cases:
   old=execute('baseline',id,entry);new=execute('current',id,entry);same=old['trace']==new['trace']
   row={'id':len(rows),'name':title,'pass':old['ok'] and new['ok'] and same,'baseline_ok':old['ok'],'current_ok':new['ok'],'trace_equal':same,'trace_records':len(new['trace'])}
   if not row['pass']:
    row['baseline']={k:v for k,v in old.items() if k!='trace'};row['current']={k:v for k,v in new.items() if k!='trace'}
    for i,(a,b) in enumerate(zip(old['trace'],new['trace'])):
     if a!=b:row['first_difference']={'index':i,'old':a,'new':b};break
   (OUT/(str(row['id'])+'_trace.json')).write_text(json.dumps({'baseline':old,'current':new}),encoding='utf-8')
   rows.append(row);print(row['id'],title,'PASS' if row['pass'] else 'FAIL',flush=True)
 for id,title in enumerate(['Ready authority and pure getters','FPS private counter','Snapshot private filename']):
  x=execute('current',id,'B3_AuthorityRun');
  if id: x['ok']=x['ok'] and not execute('baseline',id,'B3_AuthorityRun')['ok']
  rows.append({'id':len(rows),'name':title,'pass':x['ok'],'failure_line':x['return'],'error':x['error']});print(title,rows[-1],flush=True)
 old=execute('baseline',4,'B3_TaskRun');new=execute('current',4,'B3_TaskRun')
 old_calls=sum(r[0]==5 for r in old['trace']);new_calls=sum(r[0]==5 for r in new['trace'])
 rows.append({'id':len(rows),'name':'Task gate ignores diagnostic ready tamper','pass':old['ok'] and new['ok'] and new_calls>old_calls,'baseline_process_owner_checks':old_calls,'current_process_owner_checks':new_calls})
 print(rows[-1],flush=True)
 summary={'total':len(rows),'passed':sum(r['pass'] for r in rows),'failed':sum(not r['pass'] for r in rows)}
 (HERE/'results.json').write_text(json.dumps({'baseline':BASE,'summary':summary,'cases':rows,'hardware_verified':False,'real_sources':['app_storage.c','storage_task.c','jpeg_frame.c','mjpeg_avi.c'],'stubs':['StorageFS medium','RTOS environment','Media lease boundary']},indent=2),encoding='utf-8')
 print(summary);raise SystemExit(bool(summary['failed']))
