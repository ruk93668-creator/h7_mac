"""ARMCC5 O0/O2 mailbox tests and real Control/FreeRTOS queue integration."""
from pathlib import Path
import os,sys,json
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/CameraModuleR2/watch_tests';OUT.mkdir(parents=True,exist_ok=True)
os.environ['C1_TEST_OUTPUT_ROOT']=str(OUT)
sys.path.insert(0,str(ROOT/'Tests/CameraLifecycleC1'))
import run_tests as c
rows=[]
for opt in ['O0','O2']:
 c.cc=[('-'+opt if x in ['-O0','-O2'] else x) for x in c.cc]
 defs=['-DAPP_R2_HW_ACCEPTANCE_ENABLED=1','-I',str(HERE)]
 for group,source,entry,count in [('mailbox','hardware_control_test.c','R2_MailboxRun',30),('control','control_integration_test.c','R2_ControlIntegrationRun',4)]:
  sources=[HERE/source]
  flags=defs[:]
  if group=='control':
   sources+=[ROOT/'CM7/Application/app_camera_lifecycle.c']
   flags+=['-DC1_MESSAGES_SOURCE="'+(ROOT/'CM7/Application/app_messages.c').as_posix()+'"','-DR2_CONTROL_SOURCE="'+(ROOT/'CM7/Application/Tasks/control_task.c').as_posix()+'"']
  name=group+'_'+opt;c.build(name,sources,entry,flags)
  for id in range(count):
   r=c.execute(name,entry,id);r.pop('trace',None);rows.append({'group':group,'optimization':opt,'id':id,**r});print(group,opt,id,'PASS' if r['pass'] else 'FAIL',r['failure_line'],r['return'],r['error'],flush=True)
summary={'total':len(rows),'passed':sum(r['pass'] for r in rows),'failed':sum(not r['pass'] for r in rows)}
(HERE/'watch_results.json').write_text(json.dumps({'summary':summary,'cases':rows,'hardware_verified':False,'real_control_and_queue':True,'storage_request_boundaries_mocked':True},indent=2),encoding='utf-8')
print(summary);raise SystemExit(bool(summary['failed']))
