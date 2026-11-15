"""Record unchanged legacy assertions, full transports and actual per-call states."""
from pathlib import Path
import sys,os,subprocess,json,hashlib,struct,time,re,gzip
old_id7='--old-id7' in sys.argv
H=Path(__file__).resolve().parent;ROOT=H.parents[2];OUT=ROOT/'output/ConsumerJoint'/('B2B3_probe_'+str(time.time_ns()));OUT.mkdir(parents=True)
os.environ['C1_TEST_OUTPUT_ROOT']=str(OUT/'arm');sys.path.insert(0,str(ROOT/'Tests/CameraLifecycleC1'));import run_tests as c
c.cc+=['-I',OUT,'-I',ROOT/'Tests/ConsumerIntegrationB2B6'];c.sc.write_text(c.sc.read_text().replace('0x80000','0x100000'))
(OUT/'jpeg_data.h').write_text('static const unsigned char jpeg_data[]={'+','.join(map(str,(ROOT/'Tests/CameraJpegStage03A/Hardware/VerifiedCapture/jpeg_complete.jpg').read_bytes()))+'};\n')
pre=r"""#include "app_messages.h"
#include <string.h>
typedef struct {uint32_t line,passed,expression,trace_index;} AuditCheck;
typedef struct {uint32_t life,channel,trace_index;int32_t result;unsigned char bytes[56];} AuditMessage;
volatile AuditCheck audit_checks[256];volatile uint32_t audit_check_count,audit_dropped;
volatile AuditMessage audit_messages[256];volatile uint32_t audit_message_count;
bool Audit_Require(bool ok,uint32_t line,const char*expression);
bool Audit_Reject(bool bad,uint32_t line,const char*expression);
"""
post=r"""
bool Audit_Require(bool ok,uint32_t line,const char*expression){uint32_t i=audit_check_count;if(i>=256){audit_dropped++;return ok;}audit_checks[i].line=line;audit_checks[i].passed=ok;audit_checks[i].expression=(uint32_t)expression;audit_checks[i].trace_index=g_trace_count;audit_check_count++;return ok;}
bool Audit_Reject(bool bad,uint32_t line,const char*expression){Audit_Require(!bad,line,expression);return bad;}
void Audit_Message(uint32_t life,uint32_t channel,const void*m,SysResult_t result){uint32_t i=audit_message_count;if(i>=256){audit_dropped++;return;}audit_messages[i].life=life;audit_messages[i].channel=channel;audit_messages[i].trace_index=g_trace_count;audit_messages[i].result=result;memcpy((void*)audit_messages[i].bytes,m,life?56:52);audit_message_count++;}
"""
def instrument(original,group,candidate):
 s=original;patches=[]
 if group=='storage':
  old='#define CHECK(x) do { if(!(x)) return __LINE__; } while(0)';new='#define CHECK(x) do { if(!Audit_Require(!!(x),__LINE__,#x)) return __LINE__; } while(0)';assert old in s;s=s.replace(old,new);patches.append({'old':old,'new':new})
 else:
  # Only instrument existing rejecting guards; preserve expression, evaluation count and branch.
  items=[]
  for m in re.finditer(r'\bif\s*\(',s):
   start=s.index('(',m.start());depth=1;i=start+1
   while depth and i<len(s):
    if s[i]=='(':depth+=1
    elif s[i]==')':depth-=1
    i+=1
   if re.match(r'\s*return\s+__LINE__\s*;',s[i:]):
    cond=s[start+1:i-1];items.append((start+1,i-1,'Audit_Reject(('+cond+'),__LINE__,'+json.dumps(cond)+')'))
  for a,b,v in reversed(items):s=s[:a]+v+s[b:]
  # Ternary requirements are existing original predicates, not new replacements.
  s=re.sub(r'return ([^;\n]+?)\?0:__LINE__;',lambda m:'return Audit_Require(('+m[1]+'),__LINE__,'+json.dumps(m[1])+')?0:__LINE__;',s)
 has_std='SysResult_t App_MessagesSend(' in s
 if has_std:s=s.replace('SysResult_t App_MessagesSend(','SysResult_t Audit_OriginalSend(')
 if candidate:
  if group=='usb':s=s.replace('UsbTask_Process(events);UsbTask_PublishStatus();','UsbTask_Process(events);Audit_PumpUsbTask();UsbTask_PublishStatus();').replace('App_USBHandleCommand(&command);','App_USBHandleCommand(&command);B2B6_PumpUsb();')
  else:s=s.replace('App_StorageProcess();Trace(90,0,0);','App_StorageProcess();B2B6_PumpStorage();Trace(90,0,0);')
 s=pre+('\nstatic void Audit_PumpUsbTask(void);' if group=='usb' and candidate else '')+'\n#include "fixture_declarations.h"\n#line 1 "'+('Tests/UsbModuleB2/behavior_test.c' if group=='usb' else 'Tests/StorageModuleB3/behavior_test.c')+'"\n'+s+'\n'+post
 if has_std:s+='\nSysResult_t App_MessagesSend(AppChannel_t c,const AppMessage_t*m){SysResult_t r=Audit_OriginalSend(c,m);Audit_Message(0,c,m,r);return r;}\n'
 if candidate:
  s+='\n#include "audit_authorization.h"\nSysResult_t App_LifecycleMessagesSend(AppLifecycleChannel_t c,const AppLifecycleMessage_t*m){SysResult_t r=Audit_OriginalLifeSend(c,m);Audit_Message(1,c,m,r);return r;}\n'
  if group=='usb':s+='\nstatic void Audit_PumpUsbTask(void){AppCameraConsumerStatus_t v;App_USBGetLifecycleStatus(&v);if(v.phase==APP_CONSUMER_QUERY||v.phase==APP_CONSUMER_ACQUIRE){B2B6_PumpUsb();App_USBFinishRound(USB_Mode_GetMode()==USB_MODE_UVC);UsbTask_Reply();}}\nuint32_t Platform_SyncGetPrimask(void){return 0;}\nuint32_t Platform_SyncGetBasepri(void){return 0;}\n'
 return s
ah=(ROOT/'Tests/ConsumerIntegrationB2B6/authorization_fixture.h').read_bytes().decode('utf-8',errors='replace')
# Header has ASCII interfaces; leave its body unchanged apart from its test transport name.
ah=ah.replace('SysResult_t App_LifecycleMessagesSend(','SysResult_t Audit_OriginalLifeSend(');(OUT/'audit_authorization.h').write_text(ah,encoding='utf-8')
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_CODE,UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC
D=H/('RawResults/OldID7Reproduction' if old_id7 else 'RawResults/Current');D.mkdir(parents=True,exist_ok=True);results=[]
common=[ROOT/'CM7/Application'/n for n in ['app_camera_consumer.c','app_camera_qualification.c','app_camera_lifecycle.c','app_camera_lifecycle_replay.c']]+[ROOT/'Tests/ConsumerIntegrationC2_2B/legacy_transport_fixture.c']
for opt in ['O0','O2']:
 c.cc=[('-'+opt if x in ['-O0','-O2'] else x) for x in c.cc]
 for group,folder,base in [('usb','UsbModuleB2','8bfa9bc4316c0faaceef65bc90fbe84d03eae55f'),('storage','StorageModuleB3','d725a698203b3bcde479a290714a75b16bc9de16')]:
  if old_id7 and group!='usb':continue
  original=(ROOT/'Tests'/folder/'behavior_test.c').read_text(encoding='utf-8')
  for candidate in [False,True]:
   if old_id7 and not candidate:continue
   version='candidate' if candidate else 'baseline';name=group+'_'+version+'_'+opt;src=OUT/(name+'.c');src.write_text(instrument(original,group,candidate),encoding='utf-8')
   files=[]
   rels=['CM7/Application/Tasks/usb_task.c'] if group=='usb' else ['CM7/Application/app_storage.c','CM7/Application/Tasks/storage_task.c']
   for rel in rels:
    p=ROOT/rel
    if not candidate:p=OUT/(version+'_'+Path(rel).name);p.write_bytes(subprocess.check_output(['git','-c','safe.directory='+ROOT.as_posix(),'show',base+':'+rel],cwd=ROOT))
    files.append(p)
   if group=='usb' and candidate and old_id7:
    old_usb=OUT/'old3cf_app_usb.c';old_usb.write_bytes(subprocess.check_output(['git','-c','safe.directory='+ROOT.as_posix(),'show','3cf288f:CM7/Application/app_usb.c'],cwd=ROOT))
   if group=='usb':defs=['-DB2_TASK_SOURCE="'+files[0].as_posix()+'"','-DB2_APP_SOURCE="'+(old_usb if old_id7 else ROOT/'CM7/Application/app_usb.c').as_posix()+'"']+(['-DB2B6_USB'] if candidate else ['-DB2_BASELINE']);entry='B2_TestRun';width=30
   else:defs=['-DB3_STORAGE_SOURCE="'+files[0].as_posix()+'"','-DB3_TASK_SOURCE="'+files[1].as_posix()+'"']+([] if candidate else ['-DB3_BASELINE']);entry='B3_TestRun';width=96
   c.build(name,[src,*(common if candidate else [])],entry,defs);syms,sections=c.images[name]
   with (OUT/'arm'/(name+'.axf')).open('rb') as ff:sizes={z.name:z['st_size'] for z in c.ELFFile(ff).get_section_by_name('.symtab').iter_symbols()}
   cases=[('B2_TestRun',i) for i in range(96)]+[('B2_InterfaceRun',i) for i in range(6)] if group=='usb' else [('B3_TestRun',i) for i in range(43)]+[('B3_TaskRun',i) for i in range(4)]+[('B3_AuthorityRun',i) for i in range(3)]+[('B3_TaskRun',4)]
   for id,(entry,case) in enumerate(cases):
    if old_id7 and id!=10:continue
    u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
    for a,n in [(0x08000000,0x100000),(0x20000000,0x100000),(0xD0000000,0x1000000),(0x01000000,0x1000)]:u.mem_map(a,n)
    for a,b in sections:u.mem_write(a,b)
    def scalar(n):return struct.unpack('<I',u.mem_read(syms[n],4))[0] if n in syms else 0
    stopped=[];command_events=[];process_entries=[];publications=[]
    if group=='storage':
     if 's_status_snapshot' in syms:
      def publish_hook(cpu,access,address,size,value,user):
       if address==syms['s_status_snapshot'] and scalar('mask')==1:publications.append({'trace_index':scalar('g_trace_count'),'status':list(struct.unpack('<'+str(sizes['g_storage_status']//4)+'I',cpu.mem_read(syms['g_storage_status'],sizes['g_storage_status'])))})
      u.hook_add(UC_HOOK_MEM_WRITE,publish_hook,begin=syms['s_status_snapshot'],end=syms['s_status_snapshot'])
     def command_hook(cpu,access,address,size,value,user):
      old=scalar('s_command_head')
      if value!=old and scalar('s_command_count')>0:command_events.append({'command':struct.unpack('<I',cpu.mem_read(syms['s_commands']+old*4,4))[0],'trace_index':scalar('g_trace_count'),'owner':scalar('owner')})
     u.hook_add(UC_HOOK_MEM_WRITE,command_hook,begin=syms['s_command_head'],end=syms['s_command_head']+3)
     if candidate:
      def proc_hook(cpu,a,n,user):process_entries.append({'trace_index':scalar('g_trace_count'),'debug_ready':struct.unpack('<I',cpu.mem_read(syms['g_storage_status'],4))[0],'prepared':bool(cpu.mem_read(syms['s_prepared'],1)[0])})
      u.hook_add(UC_HOOK_CODE,proc_hook,begin=syms['App_StorageProcess']&~1,end=syms['App_StorageProcess']&~1)
    if (group=='usb' and entry=='B2_TestRun' and case%32==27) or (group=='storage' and entry=='B3_TaskRun' and case>=2):
     def hook(cpu,a,n,x):
      if a==(syms['App_EventsWait']&~1) and scalar('waits')==(7 if group=='usb' else 6):stopped.append(True);cpu.emu_stop()
     u.hook_add(UC_HOOK_CODE,hook)
    u.reg_write(UC_ARM_REG_SP,0x200ffff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,case);error=None
    try:u.emu_start(syms[entry]|1,0x01000000,count=50000000)
    except Exception as ex:error=str(ex)
    checks=[]
    for n in range(scalar('audit_check_count')):
     line,ok,ptr,t=struct.unpack('<4I',u.mem_read(syms['audit_checks']+n*16,16));chars=bytearray()
     while ptr and u.mem_read(ptr,1)!=b'\0' and len(chars)<1500:chars+=u.mem_read(ptr,1);ptr+=1
     checks.append({'line':line,'pass':bool(ok),'expression':chars.decode(errors='replace'),'trace_index':t})
    msgs=[]
    for n in range(scalar('audit_message_count')):
     raw=bytes(u.mem_read(syms['audit_messages']+n*72,72));life,ch,t,rr=struct.unpack('<IIIi',raw[:16]);msgs.append({'life':bool(life),'channel':ch,'trace_index':t,'transport_result':rr,'bytes':raw[16:16+(56 if life else 52)].hex()})
    count=scalar('g_trace_count');trace=[list(x) for x in struct.iter_unpack('<'+str(width)+'I',bytes(u.mem_read(syms['g_trace'],min(count,1024)*width*4)))]
    ret=u.reg_read(UC_ARM_REG_R0);failure=scalar('g_failure');ok=not error and not failure and not scalar('audit_dropped') and all(x['pass'] for x in checks) and (bool(stopped) or(ret==0 and u.reg_read(UC_ARM_REG_PC)==0x01000000))
    consumer=list(struct.unpack('<58I',u.mem_read(syms['s_usb' if group=='usb' else 's_camera_consumer'],232))) if candidate else None
    r={'suite':'b2' if group=='usb' else 'b3','id':id,'entry':entry,'case':case,'optimization':opt,'version':version,'pass':ok,'return':ret,'failure':failure,'error':error,'audit_dropped':scalar('audit_dropped'),'checks':checks,'messages':msgs,'trace':trace,'final_consumer':consumer,'loop_exit':bool(stopped),'command_events':command_events,'process_entries':process_entries,'publications':publications,'prepared':bool(u.mem_read(syms['s_prepared'],1)[0]) if group=='storage' and 's_prepared' in syms else None}
    with gzip.open(D/(name+'_'+str(id)+'.json.gz'),'wt',encoding='utf-8') as f:json.dump(r,f)
    results.append({k:v for k,v in r.items() if k not in ['trace','messages','checks']});print(name,id,ok,ret,'checks',len(checks),'messages',len(msgs),flush=True)
(H/('old_id7_results.json' if old_id7 else 'probe_results.json')).write_text(json.dumps({'cases':results,'artifact_root':str(OUT),'raw_root':'RawResults/Current','original_assertions_unchanged':True},indent=2))

# Fail closed on any unexpected C execution outcome; authority negative controls stay negative.
unexpected=[r for r in results if r['pass'] != (not (r['version']=='baseline' and r['suite']=='b3' and r['id'] in [48,49]))]
raise SystemExit(bool(unexpected))
