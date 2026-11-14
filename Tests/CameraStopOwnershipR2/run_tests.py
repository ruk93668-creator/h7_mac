"""ARMCC5 O0/O2: real Camera/Task/Media stop paths and Qualification/Rx failures."""
from pathlib import Path
import os,sys,json,inspect,hashlib,subprocess
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/CameraStopOwnershipR2/special';OUT.mkdir(parents=True,exist_ok=True)
os.environ['C1_TEST_OUTPUT_ROOT']=str(OUT)
sys.path.insert(0,str(ROOT/'Tests/CameraLifecycleC1'))
import run_tests as c
fixture=(ROOT/'Tests/CameraModuleB1/media_test.c').read_text(encoding='utf-8')
fixture=fixture.replace('#include "behavior_test.c"','#include "'+(ROOT/'Tests/CameraModuleB1/behavior_test.c').as_posix()+'"')
fixture=fixture.replace('#include "../../CM7/Media/Src/camera_capture.c"','#include "'+(ROOT/'CM7/Media/Src/camera_capture.c').as_posix()+'"')
fixture=fixture.replace('SysResult_t BSP_DCMI_Stop(void)','SysResult_t R2_FixtureBspStop(void)')
(OUT/'media_fixture.c').write_text(fixture,encoding='utf-8')
original=c.run
def mapped(cmd):
 cmd=list(cmd)
 if str(cmd[0]).endswith('armlink.exe'):
  axf=Path(cmd[cmd.index('-o')+1]);cmd+=['--map','--list',str(axf.with_suffix('.map')),'--callgraph','--symbols']
 return original(cmd)
c.run=mapped
scope=dict(c.__dict__)
source=inspect.getsource(c.execute).replace('count=3000000','count=30000000').replace('(0x01000000,0x1000)', '(0x01000000,0x1000),(0xD0000000,0x1000000)')
exec(source,scope)
rows=[]
for opt in ['O0','O2']:
 c.cc=[('-'+opt if x in ['-O0','-O2'] else x) for x in c.cc]
 defs=['-DB1_APP_SOURCE="'+(ROOT/'CM7/Application/app_camera.c').as_posix()+'"','-DB1_TASK_SOURCE="'+(ROOT/'CM7/Application/Tasks/camera_task.c').as_posix()+'"','-DR2_MEDIA_FIXTURE="'+(OUT/'media_fixture.c').as_posix()+'"']
 c.build('stop_'+opt,[HERE/'stop_test.c'],'R2_StopRun',defs)
 c.build('decision_'+opt,[HERE/'decision_test.c',ROOT/'CM7/Application/app_camera_lifecycle.c',ROOT/'CM7/Application/app_camera_lifecycle_replay.c'],'R2_DecisionRun')
 for group,entry,count in [('stop','R2_StopRun',25),('decision','R2_DecisionRun',12)]:
  for id in range(count):
   r=scope['execute'](group+'_'+opt,entry,id);r.pop('trace',None)
   rows.append({'group':group,'optimization':opt,'id':id,**r})
   print(group,opt,id,'PASS' if r['pass'] else 'FAIL',r['return'],r['failure_line'],r['error'],flush=True)
summary={'total':len(rows),'passed':sum(r['pass'] for r in rows),'failed':sum(not r['pass'] for r in rows)}
(HERE/'results.json').write_text(json.dumps({'summary':summary,'cases':rows,'hardware_verified':False,'actual_sources':['camera_task.c','app_camera.c','camera_capture.c','app_camera_qualification.c','app_camera_lifecycle.c','app_camera_lifecycle_replay.c'],'mocked':['BSP hardware','Device callbacks','Bridge idle evidence','FIFO loan boundary','transport']},indent=2),encoding='utf-8')
print(summary);raise SystemExit(bool(summary['failed']))
