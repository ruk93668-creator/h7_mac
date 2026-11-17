from pathlib import Path
import sys,os,json,inspect
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/V2Phase2/regression';OUT.mkdir(parents=True,exist_ok=True)
os.environ['C1_TEST_OUTPUT_ROOT']=str(OUT)
sys.path.insert(0,str(ROOT/'Tests/CameraLifecycleC1'));sys.path.insert(0,str(ROOT/'output/V2Phase1/python_deps'))
import run_tests as a
scope=dict(a.__dict__);exec(inspect.getsource(a.execute).replace('count=3000000','count=150000000'),scope)
rows=[]
for opt in ['O0','O2']:
 a.cc=[('-'+opt if x in ['-O0','-O2'] else x) for x in a.cc]
 a.build('p1_'+opt,[ROOT/'Tests/V2Phase1/phase1_test.c',*sorted((ROOT/'CM7/ApplicationV2').rglob('*.c'))],'V2_Phase1Run')
 for i in range(125):rows.append({'suite':'Phase1','opt':opt,'id':i,**a.execute('p1_'+opt,'V2_Phase1Run',i)})
 a.build('replay_'+opt,[ROOT/'Tests/CameraLifecycleC2_1_1/replay_test.c',ROOT/'CM7/Application/app_camera_lifecycle_replay.c'],'C2_ReplayRun',['-DC1_MESSAGES_SOURCE="'+(ROOT/'CM7/Application/app_messages.c').as_posix()+'"'])
 for i in list(range(54))+list(range(60,74))+list(range(100,356)):
  r=(scope['execute'] if i==52 else a.execute)('replay_'+opt,'C2_ReplayRun',i);r.pop('trace',None);rows.append({'suite':'C2.1.1','opt':opt,'id':i,**r})
 a.build('abi_'+opt,[ROOT/'Tests/CameraLifecycleC2_1_1/layout_probe.c',ROOT/'CM7/Application/app_camera_lifecycle.c',ROOT/'CM7/Application/app_camera_lifecycle_replay.c'],'C2_LayoutRun')
 rows.append({'suite':'C2ABI','opt':opt,'id':0,**a.execute('abi_'+opt,'C2_LayoutRun',0)})
summary={s:{'total':sum(x['suite']==s for x in rows),'pass':sum(x['suite']==s and x['pass'] for x in rows),'fail':sum(x['suite']==s and not x['pass'] for x in rows)} for s in ['Phase1','C2.1.1','C2ABI']}
(HERE/'regression_results.json').write_text(json.dumps({'summary':summary,'cases':rows},indent=2),encoding='utf-8');print(summary);raise SystemExit(any(not x['pass'] for x in rows))
