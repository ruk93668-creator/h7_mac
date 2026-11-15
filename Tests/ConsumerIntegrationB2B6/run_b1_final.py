from pathlib import Path
import os,sys,time,json,re,struct,gzip,hashlib
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/B2B6'/('b1_'+str(time.time_ns()));OUT.mkdir(parents=True)
os.environ['C1_TEST_OUTPUT_ROOT']=str(OUT/'arm');sys.path.insert(0,str(ROOT/'Tests/CameraLifecycleC1'));import run_tests as c
c.cc+=['-I',OUT,'-I',HERE]
jpeg=(ROOT/'Tests/CameraJpegStage03A/Hardware/VerifiedCapture/jpeg_complete.jpg').read_bytes();(OUT/'jpeg_data.h').write_text('static const unsigned char jpeg_data[]={'+','.join(map(str,jpeg))+'};')
s=(ROOT/'Tests/CameraModuleB1/integration_test.c').read_text(encoding='utf-8')
# 只插入协议驱动，保留所有原断言和原操作；旧Camera STOP不能绕过新资格门控。
original=s
s=s.replace('    case_id=id;config.camera.format=', '    CHECK(B1L_Init()==SYS_OK);\n    case_id=id;config.camera.format=')
s=s.replace('Owner(RUNTIME_TARGET_CAMERA);Push(APP_MESSAGE_START,10);Round(RUNTIME_EVENT_BOOT_READY);','CHECK(B1L_StartUsb()==SYS_OK);')
assert re.findall(r'CHECK\((.*?)\);',original)==[x for x in re.findall(r'CHECK\((.*?)\);',s) if x not in ['B1L_Init()==SYS_OK','B1L_StartUsb()==SYS_OK']]
s=s.replace('App_StorageProcess();}', 'App_StorageProcess();B2B6_PumpStorage();}')
s=s.replace('#include "behavior_test.c"','#include "'+(ROOT/'Tests/CameraModuleB1/behavior_test.c').as_posix()+'"')
s=re.sub(r'#include "\.\./\.\./([^"\n]+)"',lambda m:'#include "'+(ROOT/m[1]).as_posix()+'"',s)
s='#define B2B6_REAL_CAMERA\n#include "sys_result.h"\n#include "fixture_declarations.h"\nstatic SysResult_t B1L_Init(void);\nstatic SysResult_t B1L_StartUsb(void);\n'+s+'\n#include "authorization_fixture.h"\n#include "b1_lifecycle_fixture.h"\n'
f=OUT/'fixture.c';f.write_text(s,encoding='utf-8')
defs=['-DB1_APP_SOURCE="'+(ROOT/'CM7/Application/app_camera.c').as_posix()+'"','-DB1_TASK_SOURCE="'+(ROOT/'CM7/Application/Tasks/camera_task.c').as_posix()+'"']
common=[ROOT/'CM7/Application'/n for n in ['app_camera_consumer.c','app_camera_qualification.c','app_camera_lifecycle.c','app_camera_lifecycle_replay.c']]+[ROOT/'Tests/ConsumerIntegrationC2_2B/legacy_transport_fixture.c']
c.build('b1',[f,*common],'B1_IntegrationRun',defs)
# 旧AXF仅只读执行，不重编基线或覆盖0772705证据。
old=json.loads((HERE/'b1_integration_final.json').read_text(encoding='utf-8'))
baseline=Path(old['generated_fixture']).parent/'arm/b1.axf'
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_PC

def load(path):
 with path.open('rb') as f:
  e=ELFFile(f);symbols={x.name:(x['st_value'],x['st_size'],x['st_info']['type']) for x in e.get_section_by_name('.symtab').iter_symbols()}
  sections=[(x['sh_addr'],x.data()) for x in e.iter_sections() if x['sh_flags']&2 and x['sh_type']=='SHT_PROGBITS' and x['sh_size']]
 return symbols,sections
candidate=OUT/'arm/b1.axf';images={'baseline':load(baseline),'candidate':load(candidate)}
# 消息与私有状态均通过仅读内存采样；没有向业务对象写入任何值。
def execute(label,id):
 syms,sections=images[label];u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
 for a,n in [(0x08000000,0x100000),(0x20000000,0x100000),(0xD0000000,0x1000000),(0x01000000,0x1000)]:u.mem_map(a,n)
 for a,d in sections:u.mem_write(a,d)
 u.reg_write(UC_ARM_REG_SP,0x200ffff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,id)
 funcs={v[0]&~1:k for k,v in syms.items() if v[2]=='STT_FUNC' and k.startswith(('App_','Camera','Device_','Storage','MJPEG','Jpeg','JPEG','USB_','B6_','B1L_','B2B6_','Cam','Sd','Usb','Owner'))}
 events=[];returns={}
 def memsym(name):
  v=syms.get(name)
  return bytes(u.mem_read(v[0],v[1])).hex() if v and v[1] else None
 def hook(cpu,a,n,x):
  returning=returns.pop(a,[])
  for fn,ar in returning:
   row={'event':'return','function':fn,'result_raw':cpu.reg_read(UC_ARM_REG_R0)}
   if fn in ['App_MessagesReceive','App_LifecycleMessagesReceive'] and row['result_raw']==1:
    row['received_message_hex']=bytes(cpu.mem_read(ar[1],52 if fn=='App_MessagesReceive' else 56)).hex()
   events.append(row)
  if a not in funcs:return
  fn=funcs[a];args=[cpu.reg_read(r) for r in [UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3]]
  row={'event':'call','function':fn,'args':args,'state':{k:memsym(k) for k in ['s_camera','s_camera_consumer','b1l_usb','s_camera_bridge','s_usb_bridge','g_storage_status','integration_usb_ref','integration_sd_ref','sensor','s_slots','s_avi','s_state','s_count','mounted','media_releases','sd_releases']}}
  if fn in ['App_LifecycleMessagesSend','App_CameraQualificationReceive','App_CameraConsumerReceive','App_StorageReceiveLifecycle','App_LifecycleTxStage','App_LifecycleTxAcceptReply','App_LifecycleRxReceiveRequest','App_LifecycleRxComplete']:
   ptr=args[0] if fn in ['App_CameraQualificationReceive','App_StorageReceiveLifecycle'] else args[1]
   try:row['message_words']=list(struct.unpack('<14I',cpu.mem_read(ptr,56)))
   except Exception:row['message_unreadable']=True
  if fn=='App_MessagesSend':row['frame_or_control_message_hex']=bytes(cpu.mem_read(args[1],52)).hex()
  events.append(row);returns.setdefault(cpu.reg_read(UC_ARM_REG_LR)&~1,[]).append((fn,args))
 u.hook_add(UC_HOOK_CODE,hook);error=None
 try:u.emu_start(syms['B1_IntegrationRun'][0]|1,0x01000000,count=50000000)
 except Exception as e:error=str(e)
 result=u.reg_read(UC_ARM_REG_R0)
 count=struct.unpack('<I',u.mem_read(syms['g_trace_count'][0],4))[0]
 trace=[list(x) for x in struct.iter_unpack('<25I',bytes(u.mem_read(syms['g_trace'][0],min(count,1024)*100)))]
 return {'return':result,'pass':result==0 and error is None and u.reg_read(UC_ARM_REG_PC)==0x01000000,'error':error,'events':events,'legacy_trace':trace,'final_state':{k:memsym(k) for k in ['s_camera','s_camera_consumer','b1l_usb','s_camera_bridge','s_usb_bridge','g_storage_status']}}
generated_fixture_path=str(f)
rows=[];dest=HERE/'Evidence/b1_minimal';dest.mkdir(parents=True,exist_ok=True)
for id in range(11):
 pair={k:execute(k,id) for k in images}
 with gzip.open(dest/(str(id)+'.json.gz'),'wt',encoding='utf-8') as f:json.dump(pair,f)
 old=pair['baseline'];new=pair['candidate'];i=next((i for i,(a,b) in enumerate(zip(old['events'],new['events'])) if a['function']!=b['function'] or a['event']!=b['event']),None)
 row={'id':id,'baseline_return':old['return'],'candidate_return':new['return'],'pass':new['pass'],'first_event_difference':i,'trace':str(dest/(str(id)+'.json.gz')),'classification':'D' if not new['pass'] else 'passed'}
 rows.append(row);print(row,flush=True)
result={'cases':rows,'baseline_axf':str(baseline),'candidate_axf':str(candidate),'baseline_sha256':hashlib.sha256(baseline.read_bytes()).hexdigest(),'candidate_sha256':hashlib.sha256(candidate.read_bytes()).hexdigest(),'generated_fixture':generated_fixture_path,'original_assertions_preserved':True,'id_gate':sorted(r['id'] for r in rows)==list(range(11)),'release_proof_supplied':False}
(HERE/'b1_minimal_results.json').write_text(json.dumps(result,indent=2))

raise SystemExit(0 if all(r['pass'] for r in rows) else 1)
