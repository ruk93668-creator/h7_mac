from pathlib import Path
import os,sys,inspect,json,time
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/ConsumerIntegrationC2_2B'/('business_'+str(time.time_ns()));OUT.mkdir(parents=True)
os.environ['C1_TEST_OUTPUT_ROOT']=str(OUT)
sys.path.insert(0,str(ROOT/'Tests/CameraLifecycleC1'));import run_tests as c
jpeg=(ROOT/'Tests/CameraJpegStage03A/Hardware/VerifiedCapture/jpeg_complete.jpg').read_bytes()
(OUT/'jpeg_data.h').write_text('static const unsigned char jpeg_data[]={'+','.join(map(str,jpeg))+'};')
c.cc+=['-I',OUT]
c.sc.write_text(c.sc.read_text().replace('0x80000','0x100000'))
scope=dict(c.__dict__);exec(inspect.getsource(c.execute).replace('count=3000000','count=50000000').replace('(0x20000000,0x80000)','(0x20000000,0x100000)').replace('0x2007fff0','0x200ffff0').replace('(0x01000000,0x1000)','(0x01000000,0x1000),(0xD0000000,0x1000000)'),scope)
common=[ROOT/'CM7/Application/app_camera_consumer.c',ROOT/'CM7/Application/app_camera_qualification.c',ROOT/'CM7/Application/app_camera_lifecycle.c',ROOT/'CM7/Application/app_camera_lifecycle_replay.c',HERE/'legacy_transport_fixture.c']
rows=[]
for opt in ['O0','O2']:
 c.cc=[('-'+opt if x in ['-O0','-O2'] else x) for x in c.cc]
 for group,entry,count,defs in [('usb','UsbBusiness_Run',10,['-DB2_APP_SOURCE="'+(ROOT/'CM7/Application/app_usb.c').as_posix()+'"','-DB2_TASK_SOURCE="'+(ROOT/'CM7/Application/Tasks/usb_task.c').as_posix()+'"']),('storage','StorageBusiness_Run',9,['-DB3_STORAGE_SOURCE="'+(ROOT/'CM7/Application/app_storage.c').as_posix()+'"','-DB3_TASK_SOURCE="'+(ROOT/'CM7/Application/Tasks/storage_task.c').as_posix()+'"']),('control','ControlIntegration_Run',6,['-DB1_APP_SOURCE="'+(ROOT/'CM7/Application/app_camera.c').as_posix()+'"','-DB1_TASK_SOURCE="'+(ROOT/'CM7/Application/Tasks/camera_task.c').as_posix()+'"']),('camera_task','CameraTaskIntegration_Run',6,['-DB1_APP_SOURCE="'+(ROOT/'CM7/Application/app_camera.c').as_posix()+'"','-DB1_TASK_SOURCE="'+(ROOT/'CM7/Application/Tasks/camera_task.c').as_posix()+'"'])]:
  file='camera_task_test.c' if group=='camera_task' else group+'_business_test.c';name=group+'_'+opt
  c.build(name,[HERE/file,*common],entry,defs)
  for id in range(count):
   r=scope['execute'](name,entry,id);r.pop('trace',None);rows.append({'group':group,'id':id,'optimization':opt,**r});print(group,opt,id,r,flush=True)
summary={'total':len(rows),'passed':sum(x['pass'] for x in rows),'failed':sum(not x['pass'] for x in rows)}
(HERE/'business_results.json').write_text(json.dumps({'summary':summary,'cases':rows,'hardware_verified':False,'transport_mocked':True,'BSP_and_filesystem_mocked':True,'release_evidence_missing':True,'artifact_root':str(OUT)},indent=2),encoding='utf-8');print(summary);raise SystemExit(bool(summary['failed']))
