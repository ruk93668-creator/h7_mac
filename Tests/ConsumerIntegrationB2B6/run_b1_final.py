from pathlib import Path
import os,sys,time,json,re
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/B2B6'/('b1_'+str(time.time_ns()));OUT.mkdir(parents=True)
os.environ['C1_TEST_OUTPUT_ROOT']=str(OUT/'arm');sys.path.insert(0,str(ROOT/'Tests/CameraLifecycleC1'));import run_tests as c
c.cc+=['-I',OUT,'-I',HERE]
jpeg=(ROOT/'Tests/CameraJpegStage03A/Hardware/VerifiedCapture/jpeg_complete.jpg').read_bytes();(OUT/'jpeg_data.h').write_text('static const unsigned char jpeg_data[]={'+','.join(map(str,jpeg))+'};')
s=(ROOT/'Tests/CameraModuleB1/integration_test.c').read_text(encoding='utf-8')
# 只插入协议驱动，保留所有原断言和原操作；旧Camera STOP不能绕过新资格门控。
s=s.replace('App_StorageProcess();}', 'App_StorageProcess();B2B6_PumpStorage();}')
s=s.replace('#include "behavior_test.c"','#include "'+(ROOT/'Tests/CameraModuleB1/behavior_test.c').as_posix()+'"')
s=re.sub(r'#include "\.\./\.\./([^"\n]+)"',lambda m:'#include "'+(ROOT/m[1]).as_posix()+'"',s)
s='#define B2B6_REAL_CAMERA\n#include "fixture_declarations.h"\n'+s+'\n#include "authorization_fixture.h"\n'
f=OUT/'fixture.c';f.write_text(s,encoding='utf-8')
defs=['-DB1_APP_SOURCE="'+(ROOT/'CM7/Application/app_camera.c').as_posix()+'"','-DB1_TASK_SOURCE="'+(ROOT/'CM7/Application/Tasks/camera_task.c').as_posix()+'"']
common=[ROOT/'CM7/Application'/n for n in ['app_camera_consumer.c','app_camera_qualification.c','app_camera_lifecycle.c','app_camera_lifecycle_replay.c']]+[ROOT/'Tests/ConsumerIntegrationC2_2B/legacy_transport_fixture.c']
c.build('b1',[f,*common],'B1_IntegrationRun',defs)
rows=[]
for id in range(11):
 r=c.execute('b1','B1_IntegrationRun',id);r.pop('trace',None);rows.append({'id':id,**r});print(id,r,flush=True)
(HERE/'b1_integration_final.json').write_text(json.dumps({'cases':rows,'generated_fixture':str(f),'original_assertions_changed':False,'release_proof_supplied':False},indent=2))
