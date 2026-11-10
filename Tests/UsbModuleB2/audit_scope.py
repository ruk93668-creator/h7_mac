"""B2 pre-merge scope and statement mapping audit; static evidence only."""
from pathlib import Path
import subprocess,re,json,hashlib,argparse
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--baseline',default='8bfa9bc4316c0faaceef65bc90fbe84d03eae55f');args=p.parse_args()
def git(*a):return subprocess.check_output(['git','-c','safe.directory='+ROOT.as_posix(),*a],cwd=ROOT)
def text(raw):
 try:s=raw.decode('utf-8-sig')
 except UnicodeDecodeError:s=raw.decode('gbk')
 return s.replace('\r\n','\n')
def old(path):return text(git('show',args.baseline+':'+path))
def read(path):return text((ROOT/path).read_bytes())
def norm(s):return re.sub(r'\s+','',re.sub(r'//[^\n]*|/\*.*?\*/','',s,flags=re.S))
def braced(s,start):
 a=s.index('{',start);n=1;i=a+1
 while n:
  if s[i]=='{':n+=1
  elif s[i]=='}':n-=1
  i+=1
 return s[a:i]
def body(s,name):
 m=re.search(r'\b'+name+r'\([^;{}]*\)\s*\{',s);return braced(s,m.start())
tp='CM7/Application/Tasks/usb_task.c';ap='CM7/Application/app_usb.c';hp='CM7/Application/app_usb.h';proj='MDK-ARM/h7_mac.uvprojx'
before=old(tp);task=read(tp);app=read(ap)
checks={}
for name in ['UsbTask_Create','UsbTask_Run','UsbTask_PublishStatus']:
 target=body(task,name).replace('App_USBSetDiagnosticObserver(UsbTask_Observe);','')
 checks[name+'_retained']=norm(body(before,name))==norm(target)
original=body(before,'UsbTask_Process')
checks['SOF_observation_retained']=norm(original[original.index('sof ='):original.index('UsbTask_Reply();')])==norm(body(task,'UsbTask_Process')[body(task,'UsbTask_Process').index('sof ='):body(task,'UsbTask_Process').index('UsbTask_Reply();')])
mapping={'s_usb_started':'s_usb.started','s_usb_stopping':'s_usb.stopping','s_usb_stop_command':'s_usb.stop_command','s_usb_ack_pending':'s_usb.ack_pending','s_usb_ack':'s_usb.ack','s_usb_fault_reported':'s_usb.fault_reported','s_usb_fault':'s_usb.fault','s_usb_fps_tick':'s_usb.fps_tick','s_usb_fps_frames':'s_usb.fps_frames','g_usb_task_debug.command_id':'s_usb.command_id','g_usb_task_debug.result':'s_usb.last_result','g_usb_task_debug.completed_frames':'s_usb.completed_frames','g_usb_task_debug.measured_fps_milli':'s_usb.measured_fps_milli'}
def mapped(s):
 for a,b in mapping.items():s=s.replace(a,b)
 return s
def strip_observe(s):return re.sub(r'App_USBObserve\([^;]*\);','',s)
cs=original.index('s_usb_ack = command;');ce=original.index('\n    }\n\n    if (s_usb_started')
cmd=mapped(original[cs:ce]).replace('= command;','= *command;').replace('command.','command->')
current=body(app,'App_USBHandleCommand')[1:-1]
current=current[current.index('s_usb.ack = *command;'):]
checks['command_statements_moved_exactly']=norm(cmd)==norm(strip_observe(current))
ms=original.index('if (s_usb_started');me=original.index('    if (uvc)',ms)
current=body(app,'App_USBProcess')[1:-1].replace('SysResult_t result;','')
checks['mode_statements_moved_exactly']=norm(mapped(original[ms:me]))==norm(strip_observe(current))
fs=original.index('    if (s_usb_stopping)\n    {',me);fe=original.index('    UsbTask_Reply();',fs)
finish=mapped(original[fs:fe]).replace('else if (uvc &&','else if (uvc_this_round &&')
finish=finish.replace('g_usb_task_debug.state = s_usb.stopping ? 2U : (s_usb.started ? 1U : 0U);','')
current=body(app,'App_USBFinishRound')[1:-1]
for decl in ['UsbUvcStreamStatus_t status;','SysResult_t result;','uint32_t now, elapsed, frames, total;']:current=current.replace(decl,'')
checks['FinishRound_statements_moved_exactly']=norm(finish)==norm(strip_observe(current))
old_data='if (uvc)'+braced(original,me)
bp=body(task,'UsbTask_Process');dp=bp.index('if (uvc)')
new_data='if (uvc)'+braced(bp,dp)
new_data=new_data.replace('App_USBIsStopping()','s_usb_stopping').replace('App_USBCanTransfer()','s_usb_started && !s_usb_stopping')
checks['explicit_data_plane_retained']=norm(old_data)==norm(new_data)
checks['no_data_plane_hidden_in_finish']=all(x not in strip_observe(body(app,'App_USBFinishRound')) for x in ['USB_UVC_Process(','USB_UVC_CancelFrame(','App_UvcBridgeUsbProcess('])
checks['no_freertos_calls_in_business']=all(x not in app for x in ['xTask','xQueue','App_MessagesSend(','App_MessagesReceive(','App_EventsWait('])
for key,value in [('STACK_WORDS','2048U'),('PRIORITY','4U'),('WAIT_MS','20U'),('STACK_SAMPLE_MS','1000U')]:checks['USB_TASK_'+key+'_retained']=bool(re.search(r'#define USB_TASK_'+key+r'\s+'+value,task))
old_project=old(proj);project=read(proj)
checks['CM4_project_prefix_unchanged']=old_project.split('<TargetName>h7_mac_CM7</TargetName>')[0]==project.split('<TargetName>h7_mac_CM7</TargetName>')[0]
for name in ['app_usb.c','app_usb.h']:
 pattern=r'            <File>\n              <FileName>'+re.escape(name)+r'</FileName>\n              <FileType>[15]</FileType>\n              <FilePath>../CM7/Application/'+re.escape(name)+r'</FilePath>\n            </File>\n'
 project,n=re.subn(pattern,'',project);checks[name+'_registered_once']=n==1
checks['project_only_adds_CM7_entries']=project==old_project
allowed={tp,ap,hp,proj}
changed=set(git('diff','--name-only',args.baseline,'--','CM7','CM4','Common','Board','Drivers','Middlewares','MDK-ARM').decode().splitlines())
changed|=set(git('ls-files','--others','--exclude-standard','--','CM7','CM4','Common','Board','Drivers','Middlewares','MDK-ARM').decode().splitlines())
checks['production_scope_exact']=changed==allowed
historical=git('diff','--name-only',args.baseline,'--','Tests',':(exclude)Tests/UsbModuleB2').decode().splitlines()
checks['historical_tests_unchanged']=not historical
checks['main_not_modified']=git('rev-parse','main').decode().strip()==args.baseline
frozen=['CM7/Service/usb_mode.c','CM7/Service/usb_mode.h','CM7/Platform/usb_device.c','CM7/Platform/usb_device.h','CM7/Service/usb_uvc_class.c','CM7/Service/usb_uvc.h','CM7/Service/usb_uvc_desc.c','CM7/Service/usb_uvc_desc.h','CM7/Service/usb_uvc_payload.c','CM7/Service/usb_uvc_payload.h','CM7/Application/app_uvc_bridge.c','CM7/Application/app_uvc_bridge.h','CM7/Application/app_messages.c','CM7/Application/app_messages.h','CM7/Application/app_events.c','CM7/Application/app_events.h','CM7/Application/Tasks/usb_task.h']
checks['frozen_modules_byte_identical']=all((ROOT/f).read_bytes()==git('show',args.baseline+':'+f) for f in frozen)
report={'baseline':args.baseline,'checks':checks,'production_sha256':{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sorted(allowed)},'frozen_sha256':{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in frozen},'changed_production_files':sorted(changed),'historical_changes':historical,'note':'Pre-merge static scope/mapping audit, not a hardware or runtime test.'}
(HERE/'scope_audit.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(checks,indent=2));raise SystemExit(not all(checks.values()))
