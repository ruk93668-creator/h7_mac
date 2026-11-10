"""C1 ARMCC5 protocol and real FreeRTOS queue tests, no physical devices."""
from pathlib import Path
import subprocess,json,sys,struct
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/CameraLifecycleC1';OUT.mkdir(parents=True,exist_ok=True)
BASE='35b4fb7f5c39f7339b0bf0aa2a68594fd9748e7f'
sys.path.insert(0,str(ROOT/'Tests/OledAsyncAcceptance/python_deps'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile
commands=[]
def run(cmd):
 p=subprocess.run(list(map(str,cmd)),cwd=ROOT,capture_output=True)
 text=(p.stdout+p.stderr).decode('utf-8',errors='replace')
 commands.append({'command':list(map(str,cmd)),'exit':p.returncode,'output':text})
 (OUT/'commands.json').write_text(json.dumps(commands,indent=2),encoding='utf-8')
 if p.returncode:raise RuntimeError(text)
 return text
cc=['D:/32/ARM/ARMCC/Bin/armcc.exe','--cpu','Cortex-M7.fp.sp','--c99','-O0','-DSTM32H747xx','-DCORE_CM7','-DUSE_HAL_DRIVER','--preinclude',ROOT/'CM7/BSP/bsp_usb_hal_config.h']
for inc in ['CM7/Core/Inc','CM7/Device/Inc','Board','Board/Inc','Middlewares/Third_Party/FreeRTOS/Source/include','Middlewares/Third_Party/FreeRTOS/Source/portable/RVDS/ARM_CM7/r0p1','Drivers/STM32H7xx_HAL_Driver/Inc','Drivers/CMSIS/Include','Drivers/CMSIS/Device/ST/STM32H7xx/Include','CM7/Application','CM7/Application/Tasks','Common/Inc','CM7/Media/Inc','CM7/BSP','CM7/Platform','CM7/Service','CM7/Framework','Middlewares/ST/STM32_USB_Device_Library/Core/Inc']:
 cc+=['-I',ROOT/inc]


sc=OUT/'test.sct';sc.write_text('LR_TEST 0x08000000 0x100000 { ER_INIT 0x08000000 { *(.framework_init) } ER_TEST +0 { .ANY (+RO) } RW_TEST 0x20000000 0x80000 { .ANY (+RW +ZI) } }\n')
images={}
def build(name,sources,entry,defs=[]):
 objs=[]
 for i,src in enumerate(sources):
  obj=OUT/(name+str(i)+'.o');run(cc+defs+['-c',src,'-o',obj]);objs.append(obj)
 link_sc=sc
 if name!='legacy_current':
  link_sc=OUT/(name+'.sct');link_sc.write_text(sc.read_text().replace('ER_INIT 0x08000000 { *(.framework_init) } ER_TEST +0','ER_TEST 0x08000000'))
 axf=OUT/(name+'.axf');run(['D:/32/ARM/ARMCC/Bin/armlink.exe',*objs,'--entry',entry,'--scatter',link_sc,'-o',axf])
 with axf.open('rb') as f:
  e=ELFFile(f);syms={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()};sections=[(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
 images[name]=(syms,sections)
def execute(name,entry,id):
 syms,sections=images[name];u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
 for a,n in [(0x08000000,0x100000),(0x20000000,0x80000),(0xE000E000,0x2000),(0x01000000,0x1000)]:u.mem_map(a,n)
 for a,d in sections:u.mem_write(a,d)
 u.reg_write(UC_ARM_REG_SP,0x2007fff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,id)
 error=None
 try:u.emu_start(syms[entry]|1,0x01000000,count=3000000)
 except Exception as ex:error=str(ex)
 line=u.reg_read(UC_ARM_REG_R0);failure=struct.unpack('<I',u.mem_read(syms['g_failure'],4))[0]
 result={'pass':not error and not line and not failure and u.reg_read(UC_ARM_REG_PC)==0x01000000,'return':line,'failure_line':failure,'error':error}
 if 'g_trace_count' in syms:
  count=struct.unpack('<I',u.mem_read(syms['g_trace_count'],4))[0];raw=bytes(u.mem_read(syms['g_trace'],min(count,1024)*32));result['trace']=[list(x) for x in struct.iter_unpack('<8I',raw)]
 return result
if __name__=='__main__':
 build('protocol',[HERE/'protocol_test.c',ROOT/'CM7/Application/app_camera_lifecycle.c'],'C1_ProtocolRun')
 rows=[]
 for id in range(70):
  x=execute('protocol','C1_ProtocolRun',id);rows.append({'id':id,'group':'protocol',**x});print('protocol',id,x,flush=True)
 baseline=OUT/'baseline_app_messages.c';baseline.write_bytes(subprocess.check_output(['git','-c','safe.directory='+ROOT.as_posix(),'show',BASE+':CM7/Application/app_messages.c'],cwd=ROOT))
 for name,source in [('legacy_baseline',baseline),('legacy_current',ROOT/'CM7/Application/app_messages.c')]:
  defs=['-DC1_MESSAGES_SOURCE="'+source.as_posix()+'"']
  if name=='legacy_baseline':defs+=['-DC1_BASELINE']
  build(name,[HERE/'transport_test.c',ROOT/'CM7/Application/app_camera_lifecycle.c'],'C1_LegacyRun',defs)
 for id in range(60):
  old=execute('legacy_baseline','C1_LegacyRun',id);new=execute('legacy_current','C1_LegacyRun',id)
  passed=old['pass'] and new['pass'] and old['trace']==new['trace']
  (OUT/('legacy_trace_'+str(id)+'.json')).write_text(json.dumps({'baseline':old,'current':new}),encoding='utf-8')
  row={'id':len(rows),'group':'legacy_queue','case':id,'pass':passed,'baseline_ok':old['pass'],'current_ok':new['pass'],'trace_equal':old['trace']==new['trace'],'trace_records':len(new['trace'])}
  if not passed:row.update({'baseline':old,'current':new})
  rows.append(row);print('legacy',id,'PASS' if passed else 'FAIL',old['failure_line'],new['failure_line'],flush=True)
 for id in range(49):
  x=execute('legacy_current','C1_TransportRun',id);x.pop('trace',None);rows.append({'id':len(rows),'group':'lifecycle_queue','case':id,**x});print('transport',id,x,flush=True)
 for id in range(240):
  x=execute('legacy_current','C1_PermissionRun',id);x.pop('trace',None);rows.append({'id':len(rows),'group':'task_permission','case':id,**x})
  if not x['pass']:print('permission',id,x,flush=True)
 print('permission matrix',sum(x['pass'] for x in rows if x['group']=='task_permission'),'/240',flush=True)
 summary={'total':len(rows),'passed':sum(x['pass'] for x in rows),'failed':sum(not x['pass'] for x in rows)}
 for row in rows:
  n=row.get('case',row['id']);group=row['group']
  if group=='task_permission':
   v=(n%120)//5;row['name']=('Send' if n<120 else 'Receive')+f' consumer={1+v//12} op={1+(v%12)//2} kind={1+v%2} task={n%5}'
  elif group=='protocol' and n<24:row['name']=f'Wire validation consumer={1+n//12} op={1+(n%12)//2} kind={1+n%2}'
  elif group=='protocol':row['name']=['NULL and invalid message', 'Fixed field layout', 'Query assigns session', 'Query failure without session', 'Old session reply', 'Wrong request id', 'Wrong consumer reply', 'Old generation reply', 'Forged reply source', 'Wrong operation reply', 'Wrong shutdown relation', 'Current identity check', 'Batch ID reservation', 'ID overflow unchanged', 'Last ID and exhaustion', 'ID input errors', 'Queue full retry original', 'BUSY retry original', 'TIMEOUT retry original', 'IO retry original', 'Submitted request retained', 'Reply before enqueue rejected', 'Matching reply and clear', 'Late reply preserves record', 'BUSY terminal reply preserved', 'Reply enqueue completion', 'Pending overwrite refused', 'Flush reentrancy', 'Duplicate reply rejected', 'Duplicate request accepted once', 'Same key different body', 'Failure reply cannot be overwritten', 'Retire only complete cache', 'Pure cached reply read', 'Consumer cannot release qualification', 'Consumer cannot stop hardware', 'File failure and frame drain separate', 'Positive result rejected', 'Bad reply leaves output intact', 'Reply construction alias', 'Query zero-session success rejected', 'Other pending request BUSY', 'Shutdown relation echoed', 'Invalid stage preserves record', 'NULL record arguments', 'Zero current identity rejected'][n-24]
  elif group=='lifecycle_queue':row['name']=['Partial init failure 1', 'Partial init failure 2', 'Partial init failure 3', 'Init ISR rejected', 'Init PRIMASK rejected', 'Init running scheduler rejected', 'Copy consumer=1 op=1 kind=1', 'Copy consumer=1 op=1 kind=2', 'Copy consumer=1 op=2 kind=1', 'Copy consumer=1 op=2 kind=2', 'Copy consumer=1 op=3 kind=1', 'Copy consumer=1 op=3 kind=2', 'Copy consumer=1 op=4 kind=1', 'Copy consumer=1 op=4 kind=2', 'Copy consumer=1 op=5 kind=1', 'Copy consumer=1 op=5 kind=2', 'Copy consumer=1 op=6 kind=1', 'Copy consumer=1 op=6 kind=2', 'Copy consumer=2 op=1 kind=1', 'Copy consumer=2 op=1 kind=2', 'Copy consumer=2 op=2 kind=1', 'Copy consumer=2 op=2 kind=2', 'Copy consumer=2 op=3 kind=1', 'Copy consumer=2 op=3 kind=2', 'Copy consumer=2 op=4 kind=1', 'Copy consumer=2 op=4 kind=2', 'Copy consumer=2 op=5 kind=1', 'Copy consumer=2 op=5 kind=2', 'Copy consumer=2 op=6 kind=1', 'Copy consumer=2 op=6 kind=2', 'Send ISR rejected', 'Send suspended scheduler rejected', 'Send PRIMASK rejected', 'Send BASEPRI rejected', 'Forged task rejected', 'Wrong channel rejected', 'Unknown version rejected', 'Four-entry FIFO and full', 'Pending retries real full queue', 'Notification failure not duplicate enqueue', 'Wrong reader preserves output', 'Empty read preserves output', 'Camera receive wakes USB and Storage', 'Consumer receive wakes Camera', 'Full new queue leaves old queue usable', 'Old init does not reset new queue', 'Duplicate lifecycle init', 'NULL and invalid channel', 'Masked receive leaves queue intact'][n]
  elif group=='legacy_queue':row['name']=('Old send matrix' if n<25 else 'Old receive matrix' if n<50 else 'Old queue edge')+' '+str(n)
  else:row['name']=group+' scenario '+str(n)
 (HERE/'results.json').write_text(json.dumps({'summary':summary,'cases':rows,'hardware_verified':False},indent=2),encoding='utf-8')
 print(summary);raise SystemExit(bool(summary['failed']))
