from pathlib import Path
import os,sys,time,json,re,struct,gzip,hashlib
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/ConsumerJoint'/str(time.time_ns());OUT.mkdir(parents=True)
os.environ['C1_TEST_OUTPUT_ROOT']=str(OUT/'arm');sys.path.insert(0,str(ROOT/'Tests/CameraLifecycleC1'));import run_tests as c
c.cc+=['-I',OUT];c.sc.write_text(c.sc.read_text().replace('0x80000','0x100000'))
jpeg=(ROOT/'Tests/CameraJpegStage03A/Hardware/VerifiedCapture/jpeg_complete.jpg').read_bytes();(OUT/'jpeg_data.h').write_text('static const unsigned char jpeg_data[]={'+','.join(map(str,jpeg))+'};\n')
defs=['-DB1_APP_SOURCE="'+(ROOT/'CM7/Application/app_camera.c').as_posix()+'"','-DB1_TASK_SOURCE="'+(ROOT/'CM7/Application/Tasks/camera_task.c').as_posix()+'"']
common=[ROOT/'CM7/Application'/n for n in ['app_camera_consumer.c','app_camera_qualification.c','app_camera_lifecycle.c','app_camera_lifecycle_replay.c']]+[ROOT/'Tests/ConsumerIntegrationC2_2B/legacy_transport_fixture.c']
c.build('joint',[HERE/'joint_test.c',*common],'Joint_Run',defs)
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_PC

def load(path):
 with path.open('rb') as f:
  e=ELFFile(f);symbols={x.name:(x['st_value'],x['st_size'],x['st_info']['type']) for x in e.get_section_by_name('.symtab').iter_symbols()}
  sections=[(x['sh_addr'],x.data()) for x in e.iter_sections() if x['sh_flags']&2 and x['sh_type']=='SHT_PROGBITS' and x['sh_size']]
 return symbols,sections
def execute(label,id):
 syms,sections=images[label];u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
 for a,n in [(0x08000000,0x100000),(0x20000000,0x100000),(0xD0000000,0x1000000),(0x01000000,0x1000)]:u.mem_map(a,n)
 for a,d in sections:u.mem_write(a,d)
 u.reg_write(UC_ARM_REG_SP,0x200ffff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,id)
 funcs={v[0]&~1:k for k,v in syms.items() if v[2]=='STT_FUNC' and k.startswith(('App_','Camera','Device_','Storage','MJPEG','Mjpeg','Jpeg','JPEG','USB_','B6_','B1L_','B2B6_','Cam','Sd','Usb','Owner','Joint_'))}
 events=[];returns={};preparing=[True];hardware_calls={k:0 for k in ['Device_Start','Device_Stop','CameraCapture_Start','CameraCapture_Stop']}
 def memsym(name):
  v=syms.get(name)
  return bytes(u.mem_read(v[0],v[1])).hex() if v and v[1] else None
 def stamp(row):
  row['sequence']=len(events)+1
  task=struct.unpack('<I',u.mem_read(syms['integration_owner'][0],4))[0]
  row['task_id']=task;row['actor']={0:'CameraTask',1:'UsbTask',4:'StorageTask'}.get(task,'transport')
  row['hardware_call_counts']=dict(hardware_calls)
  row['operation']=row['function'];row['request_id']=None;row['session']=None;row['consumer']=None;row['generation']=None;row['lease_id']=None
  words=row.get('message_words')
  if words and words[0]==1 and words[1] in [1,2]:
   row.update(operation=words[2],source=words[3],target=words[4],session=words[5],request_id=words[6],consumer=words[7],generation=words[8])
  raw=row.get('frame_or_control_message_hex',row.get('received_message_hex'))
  if raw and len(raw)==104:
   w=struct.unpack('<13I',bytes.fromhex(raw));row.update(message_type=w[0]&255,source=(w[0]>>8)&255,command_id=w[1],stream_epoch=w[2],lease_id=w[3],message_result=w[4])
  if raw and len(raw)==112:
   w=struct.unpack('<14I',bytes.fromhex(raw));row.update(operation=w[2],source=w[3],target=w[4],session=w[5],request_id=w[6],consumer=w[7],generation=w[8])
 def hook(cpu,a,n,x):
  if preparing[0] and a==(syms['App_EventsWait'][0]&~1):cpu.emu_stop();return
  returning=returns.pop(a,[])
  for fn,ar,call_seq in returning:
   row={'event':'return','function':fn,'call_sequence':call_seq,'result_raw':cpu.reg_read(UC_ARM_REG_R0),'state_after':{k:memsym(k) for k in ['s_camera','s_usb','s_camera_consumer','s_camera_bridge','s_usb_bridge','s_slots','s_state','integration_usb_ref','integration_sd_ref']}}
   if fn=='App_CameraConsumerProcess':row['consumer_words_after']=list(struct.unpack('<58I',cpu.mem_read(ar[0],232)))
   if fn in ['App_MessagesReceive','App_LifecycleMessagesReceive'] and row['result_raw']==1:
    row['received_message_hex']=bytes(cpu.mem_read(ar[1],52 if fn=='App_MessagesReceive' else 56)).hex()
   stamp(row);events.append(row)
  if a not in funcs:return
  fn=funcs[a];args=[cpu.reg_read(r) for r in [UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3]]
  row={'event':'call','function':fn,'args':args,'state':{k:memsym(k) for k in ['s_camera','s_usb','s_camera_consumer','b1l_usb','s_camera_bridge','s_usb_bridge','g_storage_status','integration_usb_ref','integration_sd_ref','sensor','s_slots','s_avi','s_state','s_count','mounted','media_releases','sd_releases']}}
  if fn in ['App_LifecycleMessagesSend','App_CameraQualificationReceive','App_CameraConsumerReceive','App_StorageReceiveLifecycle','App_USBReceiveLifecycle','App_LifecycleTxStage','App_LifecycleTxAcceptReply','App_LifecycleRxReceiveRequest','App_LifecycleRxComplete']:
   ptr=args[0] if fn in ['App_CameraQualificationReceive','App_StorageReceiveLifecycle','App_USBReceiveLifecycle'] else args[1]
   try:row['message_words']=list(struct.unpack('<14I',cpu.mem_read(ptr,56)))
   except Exception:row['message_unreadable']=True
  if fn in ['App_MessagesSend','App_USBHandleCommand','App_CameraHandleCommand']:row['frame_or_control_message_hex']=bytes(cpu.mem_read(args[1] if fn=='App_MessagesSend' else args[0],52)).hex()
  if fn in hardware_calls:hardware_calls[fn]+=1
  stamp(row);events.append(row);returns.setdefault(cpu.reg_read(UC_ARM_REG_LR)&~1,[]).append((fn,args,row['sequence']))
 u.hook_add(UC_HOOK_CODE,hook);error=None
 u.emu_start(syms['Joint_PrepareTask'][0]|1,0x01000000,count=1000000)
 preparing[0]=False;u.reg_write(UC_ARM_REG_SP,0x200ffff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,id)
 try:u.emu_start(syms['Joint_Run'][0]|1,0x01000000,count=50000000)
 except Exception as e:error=str(e)
 result=u.reg_read(UC_ARM_REG_R0)
 count=struct.unpack('<I',u.mem_read(syms['g_trace_count'][0],4))[0]
 trace=[list(x) for x in struct.iter_unpack('<25I',bytes(u.mem_read(syms['g_trace'][0],min(count,1024)*100)))]
 return {'return':result,'pass':result==0 and error is None and u.reg_read(UC_ARM_REG_PC)==0x01000000,'error':error,'events':events,'legacy_trace':trace,'final_state':{k:memsym(k) for k in ['s_camera','s_usb','s_camera_consumer','b1l_usb','s_camera_bridge','s_usb_bridge','g_storage_status']}}

images={'candidate':load(OUT/'arm/joint.axf')};dest=HERE/'Evidence';dest.mkdir(exist_ok=True);rows=[]
for id in range(11):
 r=execute('candidate',id)
 with gzip.open(dest/(str(id)+'.json.gz'),'wt',encoding='utf-8') as f:json.dump(r,f)
 row={'id':id,'status':'PASS' if r['pass'] else 'FAIL','return':r['return'],'first_contract_violation':None if r['pass'] else {'check_line':r['return'],'event_sequence':len(r['events'])},'classification':None if r['pass'] else ('C' if id==7 else 'D')}
 rows.append(row);print(row,flush=True)
(HERE/'results.json').write_text(json.dumps({'cases':rows,'artifact_root':str(OUT),'hardware_verified':False},indent=2))
raise SystemExit(any(r['status']!='PASS' for r in rows))
