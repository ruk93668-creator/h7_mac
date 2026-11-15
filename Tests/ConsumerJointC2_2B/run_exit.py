"""Isolated Qualification Ops contract stubs; no hardware-release acceptance claim."""
from pathlib import Path
import os,sys,time,json,subprocess
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/ConsumerJoint'/('exit_'+str(time.time_ns()));OUT.mkdir(parents=True)
os.environ['C1_TEST_OUTPUT_ROOT']=str(OUT)
sys.path.insert(0,str(ROOT/'Tests/CameraLifecycleC1'));import run_tests as c
rows=[]
for opt in ['O0','O2']:
 c.cc=[('-'+opt if x in ['-O0','-O2'] else x) for x in c.cc]
 c.build('exit_'+opt,[HERE/'exit_safety_test.c',ROOT/'Tests/ConsumerIntegrationC2_2B/legacy_transport_fixture.c',*[ROOT/'CM7/Application'/n for n in ['app_camera_qualification.c','app_camera_lifecycle.c','app_camera_lifecycle_replay.c']]],'Exit_Run')
 for id in range(8):
  r=c.execute('exit_'+opt,'Exit_Run',id);rows.append({'id':'R%02d'%(id+1),'optimization':opt,'status':'PASS' if r['pass'] else 'FAIL',**r});print(rows[-1],flush=True)
(HERE/'exit_results.json').write_text(json.dumps({'cases':rows,'summary':{'unique_cases':8,'executions':len(rows),'passed':sum(r['pass'] for r in rows)},'test_ops_only':True,'real_release_proven':False,'artifact_root':str(OUT)},indent=2))
raise SystemExit(any(not r['pass'] for r in rows))
