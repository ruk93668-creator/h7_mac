"""ARMCC5 O0/O2, actual lifecycle adapter and original FreeRTOS queue implementation."""
from pathlib import Path
import os,sys,json
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/LifecycleInitBasepri/special';OUT.mkdir(parents=True,exist_ok=True)
os.environ['C1_TEST_OUTPUT_ROOT']=str(OUT)
sys.path.insert(0,str(ROOT/'Tests/CameraLifecycleC1'))
import run_tests as c
rows=[]
names=['BASEPRI0 init and repeat','BASEPRI0x50 init and repeat','Other nonzero BASEPRI init','PRIMASK reject','ISR reject','Running reject','Suspended reject','Partial creation failure latch','Runtime send BASEPRI0x50','Runtime send other nonzero','Runtime receive BASEPRI0x50 preserves queue','Runtime receive other nonzero preserves queue','Runtime normal transfer and ABI']
for opt in ['O0','O2']:
 c.cc=[('-'+opt if x in ['-O0','-O2'] else x) for x in c.cc]
 name='init_'+opt
 c.build(name,[HERE/'init_test.c',ROOT/'CM7/Application/app_camera_lifecycle.c'],'Basepri_InitRun',['-DC1_MESSAGES_SOURCE="'+(ROOT/'CM7/Application/app_messages.c').as_posix()+'"'])
 for id,title in enumerate(names):
  r=c.execute(name,'Basepri_InitRun',id);r.pop('trace',None);rows.append({'id':id,'optimization':opt,'name':title,**r})
  print(opt,id,'PASS' if r['pass'] else 'FAIL',r['failure_line'],r['error'],flush=True)
summary={'total':len(rows),'passed':sum(r['pass'] for r in rows),'failed':sum(not r['pass'] for r in rows)}
(HERE/'results.json').write_text(json.dumps({'summary':summary,'cases':rows,'real_queue':True,'mocked_port_and_task_context':True,'hardware_verified':False},indent=2),encoding='utf-8')
print(summary);raise SystemExit(bool(summary['failed']))
