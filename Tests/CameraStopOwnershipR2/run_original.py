"""ARMCC5 O0/O2: actual qualification core and original protocol tools."""
from pathlib import Path
import os,sys,json,inspect
ROOT=Path(__file__).resolve().parents[2]; HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/CameraStopOwnershipR2/original';OUT.mkdir(parents=True,exist_ok=True)
os.environ['C1_TEST_OUTPUT_ROOT']=str(OUT/'arm')
sys.path.insert(0,str(ROOT/'Tests/CameraLifecycleC1'))
import run_tests as c
original=c.run
def mapped(cmd):
 cmd=list(cmd)
 if str(cmd[0]).endswith('armlink.exe'):
  axf=Path(cmd[cmd.index('-o')+1]);cmd+=['--map','--list',str(axf.with_suffix('.map')),'--callgraph','--symbols']
 return original(cmd)
c.run=mapped
scope=dict(c.__dict__)
exec(inspect.getsource(c.execute).replace('count=3000000','count=150000000'),scope)
rows=[]
for opt in ['O0','O2']:
 c.cc=[('-'+opt if x=='-O0' else x) for x in c.cc]
 c.build('core_'+opt,[ROOT/'Tests/ConsumerQualificationC2_2A/qualification_test.c',ROOT/'CM7/Application/app_camera_lifecycle.c',ROOT/'CM7/Application/app_camera_lifecycle_replay.c'],'C2A_Run')
 for id in range(67):
  r=scope['execute']('core_'+opt,'C2A_Run',id);r.pop('trace',None)
  rows.append({'id':id,'q':f'Q{id+1:02d}' if id<44 else None,'optimization':opt,**r})
  print(opt,id,'PASS' if r['pass'] else 'FAIL',r['failure_line'],r['error'],flush=True)
for opt in ['O0','O2']:
 c.cc=[('-'+opt if x in ['-O0','-O2'] else x) for x in c.cc]
 defs=['-DB1_APP_SOURCE="'+(ROOT/'CM7/Application/app_camera.c').as_posix()+'"','-DB1_TASK_SOURCE="'+(ROOT/'CM7/Application/Tasks/camera_task.c').as_posix()+'"']
 c.build('adapter_'+opt,[ROOT/'Tests/ConsumerQualificationC2_2A/adapter_test.c',ROOT/'CM7/Application/app_camera_qualification.c',ROOT/'CM7/Application/app_camera_lifecycle.c',ROOT/'CM7/Application/app_camera_lifecycle_replay.c'],'C2A_AdapterRun',defs)
 for id in range(14):
  r=c.execute('adapter_'+opt,'C2A_AdapterRun',id);r.pop('trace',None)
  rows.append({'id':'adapter_'+str(id),'optimization':opt,**r})
  print(opt,'adapter',id,'PASS' if r['pass'] else 'FAIL',r['failure_line'],r['error'],flush=True)
summary={'total':len(rows),'passed':sum(r['pass'] for r in rows),'failed':sum(not r['pass'] for r in rows)}
(HERE/'original_results.json').write_text(json.dumps({'summary':summary,'cases':rows,'hardware_verified':False,'evidence_is_stub':True},indent=2),encoding='utf-8')
print(summary);raise SystemExit(bool(summary['failed']))
