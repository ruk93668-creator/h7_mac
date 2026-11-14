from pathlib import Path
import os,sys,json,inspect,time
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/ConsumerIntegrationC2_2B'/('special_'+str(time.time_ns()));OUT.mkdir(parents=True)
os.environ['C1_TEST_OUTPUT_ROOT']=str(OUT)
sys.path.insert(0,str(ROOT/'Tests/CameraLifecycleC1'));import run_tests as c
scope=dict(c.__dict__);exec(inspect.getsource(c.execute).replace('count=3000000','count=200000000'),scope)
rows=[]
for opt in ['O0','O2']:
 c.cc=[('-'+opt if x in ['-O0','-O2'] else x) for x in c.cc]
 name='integration_'+opt
 c.build(name,[HERE/'consumer_test.c',ROOT/'CM7/Application/app_camera_consumer.c',ROOT/'CM7/Application/app_camera_lifecycle.c',ROOT/'CM7/Application/app_camera_lifecycle_replay.c'],'Integration_Run')
 for id in range(17):
  r=scope['execute'](name,'Integration_Run',id);r.pop('trace',None);rows.append({'id':id,'optimization':opt,'release_proof_stub':id not in [4,15],**r});print(opt,id,r,flush=True)
summary={'total':len(rows),'passed':sum(x['pass'] for x in rows),'failed':sum(not x['pass'] for x in rows)}
(HERE/'results.json').write_text(json.dumps({'summary':summary,'cases':rows,'hardware_verified':False,'resource_hardware_mocked':True,'artifact_root':str(OUT)},indent=2),encoding='utf-8')
print(summary);raise SystemExit(bool(summary['failed']))
