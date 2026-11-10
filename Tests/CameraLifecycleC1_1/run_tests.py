"""Recheck all C1 scenarios and compile additional real-queue reliability interleavings."""
from pathlib import Path
import subprocess,sys,os,json
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/CameraLifecycleC1_1';OUT.mkdir(parents=True,exist_ok=True)
env=dict(os.environ,C1_TEST_OUTPUT_ROOT=str(OUT/'original419'),PYTHONIOENCODING='utf-8')
p=subprocess.run([sys.executable,str(ROOT/'Tests/CameraLifecycleC1/run_tests.py')],cwd=ROOT,env=env,capture_output=True)
(OUT/'original419.log').write_bytes(p.stdout+p.stderr)
if p.returncode:print((p.stdout+p.stderr).decode('utf-8',errors='replace'));raise SystemExit(p.returncode)
original=json.loads((ROOT/'Tests/CameraLifecycleC1/results.json').read_text(encoding='utf-8'))
assert original['summary']=={'total':419,'passed':419,'failed':0}
os.environ['C1_TEST_OUTPUT_ROOT']=str(OUT/'interleavings')
sys.path.insert(0,str(ROOT/'Tests/CameraLifecycleC1'))
import run_tests as c
c.build('c1_1',[HERE/'reliability_test.c',ROOT/'CM7/Application/app_camera_lifecycle.c'],'C1_1_ReliabilityRun',['-DC1_MESSAGES_SOURCE="'+(ROOT/'CM7/Application/app_messages.c').as_posix()+'"'])
names=['ACK not enqueued cannot retire','ACK queued unconsumed duplicate','Retire refused old request still duplicate','New session old request guarded','New generation late ACK','READY cancel repeat and retransmit','WAIT_REPLY cancel and force clear refused','SENDING cancel refused then READY cancel','COMPLETE cancel refused','COMMITTED cancel refused','Pending reply cannot be cancelled','NULL cancel','Empty and duplicate cancel','Full request queue cancelled request absent','Full ACK queue duplicate request reply retry','Bounded replay pool full no eviction','Failure cache cannot become success','Old request ID ACK rejected','Cancel leaves identity counter and notifications unchanged','Cancelled unsubmitted request sends once','Invalid and in-progress retirement','Session and generation mismatch','Duplicate completion unchanged','BUSY reply remains terminal outcome']
rows=[]
for id,name in enumerate(names):
 x=c.execute('c1_1','C1_1_ReliabilityRun',id);x.pop('trace',None);rows.append({'id':id,'name':name,**x});print(id,name,'PASS' if x['pass'] else 'FAIL',x['failure_line'],x['error'],flush=True)
summary={'original_c1':original['summary'],'added_total':len(rows),'added_passed':sum(r['pass'] for r in rows),'added_failed':sum(not r['pass'] for r in rows),'arm_total':419+len(rows)}
(HERE/'results.json').write_text(json.dumps({'summary':summary,'cases':rows,'original_contract_update':'C1 case56 retained; completed retirement must return NOT_SUPPORTED and cached reply must be unchanged.','hardware_verified':False},indent=2),encoding='utf-8')
print(summary);raise SystemExit(bool(summary['added_failed']))
