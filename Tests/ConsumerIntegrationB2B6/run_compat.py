"""Execute original legacy assertions with real lifecycle prerequisites; retain exact trace failures."""
from pathlib import Path
import sys,os,subprocess,json,hashlib,struct,inspect,time
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/B2B6'/('compat_'+str(time.time_ns()));OUT.mkdir(parents=True)
os.environ['C1_TEST_OUTPUT_ROOT']=str(OUT/'arm')
sys.path.insert(0,str(ROOT/'Tests/CameraLifecycleC1'));import run_tests as c
c.cc+=['-I',HERE,'-I',OUT]
jpeg=(ROOT/'Tests/CameraJpegStage03A/Hardware/VerifiedCapture/jpeg_complete.jpg').read_bytes();(OUT/'jpeg_data.h').write_text('static const unsigned char jpeg_data[]={'+','.join(map(str,jpeg))+'};')
c.sc.write_text(c.sc.read_text().replace('0x80000','0x100000'))
common=[ROOT/'CM7/Application'/f for f in ['app_camera_consumer.c','app_camera_qualification.c','app_camera_lifecycle.c','app_camera_lifecycle_replay.c']]+[ROOT/'Tests/ConsumerIntegrationC2_2B/legacy_transport_fixture.c']
changes=[]
def generate(group):
 old=(ROOT/('Tests/UsbModuleB2/behavior_test.c' if group=='usb' else 'Tests/StorageModuleB3/behavior_test.c')).read_text(encoding='utf-8')
 s=old
 if group=='usb':
  s=s.replace('UsbTask_Process(events);UsbTask_PublishStatus();','UsbTask_Process(events);B2B6_PumpUsb();UsbTask_PublishStatus();')
  s=s.replace('App_USBHandleCommand(&command);','App_USBHandleCommand(&command);B2B6_PumpUsb();')
 else:s=s.replace('App_StorageProcess();Trace(90,0,0);','App_StorageProcess();B2B6_PumpStorage();Trace(90,0,0);')
 # 可逆插入校验：删除新增的前置驱动调用后，原断言和全部原代码逐字恢复。
 undo=s.replace('B2B6_PumpUsb();','').replace('B2B6_PumpStorage();','')
 assert undo==old
 s='#include "fixture_declarations.h"\n'+s+'\n#include "authorization_fixture.h"\n'
 if group=='usb':s+='\nuint32_t Platform_SyncGetPrimask(void){return 0;}\nuint32_t Platform_SyncGetBasepri(void){return 0;}\n'
 p=OUT/(group+'_fixture.c');p.write_text(s,encoding='utf-8')
 changes.append({'group':group,'original_sha256':hashlib.sha256(old.encode()).hexdigest(),'all_original_code_recoverable':True,'generated':str(p)})
 return p
rows=[]
for group in ['usb','storage']:
 source=generate(group)
 if group=='usb':
  defs=['-DB2B6_USB','-DB2_APP_SOURCE="'+(ROOT/'CM7/Application/app_usb.c').as_posix()+'"','-DB2_TASK_SOURCE="'+(ROOT/'CM7/Application/Tasks/usb_task.c').as_posix()+'"'];entry='B2_TestRun';width=30
 else:
  defs=['-DB3_STORAGE_SOURCE="'+(ROOT/'CM7/Application/app_storage.c').as_posix()+'"','-DB3_TASK_SOURCE="'+(ROOT/'CM7/Application/Tasks/storage_task.c').as_posix()+'"'];entry='B3_TestRun';width=96
 c.build(group,[source,*common],entry,defs)
 syms,sections=c.images[group]
 def execute(id,entry):
  from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_CODE
  from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC
  u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
  for a,n in [(0x08000000,0x100000),(0x20000000,0x100000),(0xD0000000,0x1000000),(0x01000000,0x1000)]:u.mem_map(a,n)
  for a,d in sections:u.mem_write(a,d)
  u.reg_write(UC_ARM_REG_SP,0x200ffff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,id);stopped=[]
  is_loop=(group=='usb' and entry=='B2_TestRun' and id%32==27) or (group=='storage' and entry=='B3_TaskRun' and id>=2)
  if is_loop:
   def hook(cpu,a,n,x):
    count=struct.unpack('<I',cpu.mem_read(syms['waits'],4))[0]
    if a==(syms['App_EventsWait']&~1) and count==(7 if group=='usb' else 6):stopped.append(True);cpu.emu_stop()
   u.hook_add(UC_HOOK_CODE,hook)
  error=None
  try:u.emu_start(syms[entry]|1,0x01000000,count=50000000)
  except Exception as e:error=str(e)
  ret=u.reg_read(UC_ARM_REG_R0);fail=struct.unpack('<I',u.mem_read(syms['g_failure'],4))[0]
  count=struct.unpack('<I',u.mem_read(syms['g_trace_count'],4))[0]
  trace=[list(x) for x in struct.iter_unpack('<'+str(width)+'I',bytes(u.mem_read(syms['g_trace'],min(count,1024)*width*4)))]
  proof=list(struct.unpack('<10I',u.mem_read(syms['g_b2b6_proof'],40)))
  return {'authorization_completed':proof[0]==0 or(proof[1]>0 and proof[3]>=2 and proof[5]==0),'assertions_pass':not error and not fail and (bool(stopped) or(ret==0 and u.reg_read(UC_ARM_REG_PC)==0x01000000)),'return':ret,'failure':fail,'error':error,'proof':proof,'trace':trace}
 cases=[('B2_TestRun',i) for i in range(96)]+[('B2_InterfaceRun',i) for i in range(6)] if group=='usb' else [('B3_TestRun',i) for i in range(43)]+[('B3_TaskRun',i) for i in range(4)]+[('B3_AuthorityRun',i) for i in range(3)]+[('B3_TaskRun',4)]
 for id,(entry,case) in enumerate(cases):
  r=execute(case,entry);(OUT/(group+'_'+str(id)+'.json')).write_text(json.dumps(r));r.pop('trace');rows.append({'group':group,'id':id,'entry':entry,'case':case,**r});print(group,id,'PASS' if r['assertions_pass'] else 'FAIL',r['return'],r['error'],r['proof'],flush=True)
 (HERE/(group+'_compat.json')).write_text(json.dumps({'cases':[x for x in rows if x['group']==group],'fixture':changes[-1],'artifact_root':str(OUT),'resource_mocked':True,'release_proof_supplied':False,'original_trace_assertions_relaxed':False},indent=2))
(HERE/'compat_results.json').write_text(json.dumps({'cases':rows,'generated_fixtures':changes,'artifact_root':str(OUT)},indent=2))
