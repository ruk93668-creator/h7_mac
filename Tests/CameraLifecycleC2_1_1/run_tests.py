"""Compile actual C2.1.1 and real queue implementation with ARMCC5; emulate O0/O2."""
from pathlib import Path
import os,sys,json
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/CameraLifecycleC2_1_1';OUT.mkdir(parents=True,exist_ok=True)
os.environ['C1_TEST_OUTPUT_ROOT']=str(OUT/'arm')
sys.path.insert(0,str(ROOT/'Tests/CameraLifecycleC1'))
import run_tests as c
names=['ARMCC layout and queue sizes','Running duplicate executes once','Completed duplicate replays exact reply','Same ID modified generation conflicts','Successor while running quarantined','Successor before first ACK quarantined','Committed ACK permits safe successor','Retired request rejected','Gap request rejected','Duplicate reply backlog holds successor','Candidate duplicate coalesced','Candidate same ID changed body quarantined','Second distinct candidate quarantined','Zero-session query replay and retirement','Session and generation do not reset watermark','Malformed large ID leaves window unchanged','READY cancel burns issued ID','WAIT_REPLY timeout never clears','Result read is not consumption','Four domains isolated','Reinitialization rejected','Stage failure leaves issued unchanged','Maximum ID and exhaustion','Rollback malformed candidate','Rollback backward candidate','Rollback wrong-domain candidate','Tx callback reentry guarded','Rx callback reentry guarded','BUSY completion immutable','Failure completion immutable','Real full request queue retry','Real full reply queue retry','Notification loss does not duplicate','Candidate preserved across real full replay queue','Single pending Stage gate','Original ReplayRetire still sealed','Pure protocol status','NULL input checks','Output alias checks','Wrong domain rejected','Submission timeout preserves READY','Reply failure preserves pending','No automatic duplicate reply sends','Premature reply rejected','Late generation ACK rejected','Promotion output failure rollback','Cancel identity mismatch','Promote delivers at most once','Invalid initialization and context','Zero reservation rollback']
names+=['Temporary ReplayBegin failure rolls back all state','Mismatched pending reply cannot authorize retirement']
names+=['4096 transactions on one persistent window','Shared inbox drains other domain and reverse reply despite held candidate']
ids=list(range(54))+list(range(60,74))+list(range(100,356))
# Preserve actual linker maps and call graphs for layout/stack verification.
# Only raise the test instruction budget for the single-window endurance case.
import inspect
long_scope=dict(c.__dict__)
exec(inspect.getsource(c.execute).replace('count=3000000','count=150000000'),long_scope)
original_run=c.run
def run_with_map(cmd):
 cmd=list(cmd)
 if str(cmd[0]).endswith('armlink.exe'):
  axf=Path(cmd[cmd.index('-o')+1])
  cmd+=['--map','--list',str(axf.with_suffix('.map')),'--callgraph','--symbols']
 return original_run(cmd)
c.run=run_with_map
rows=[]
abi=[]
for optimization in ['O0','O2']:
 c.cc=[('-'+optimization if x=='-O0' else x) for x in c.cc]
 c.build('replay_'+optimization,[HERE/'replay_test.c',ROOT/'CM7/Application/app_camera_lifecycle_replay.c'],'C2_ReplayRun',['-DC1_MESSAGES_SOURCE="'+(ROOT/'CM7/Application/app_messages.c').as_posix()+'"'])
 for id in ids:
  result=(long_scope['execute'] if id==52 else c.execute)('replay_'+optimization,'C2_ReplayRun',id);result.pop('trace',None)
  name=names[id] if id<54 else 'Full identity word '+str(id-60) if id<74 else '64-cycle stress domain '+str((id-100)%4)+' sample '+str(id-100)
  rows.append({'id':id,'optimization':optimization,'name':name,**result})
  if id<74 or not result['pass']: print(optimization,id,name,'PASS' if result['pass'] else 'FAIL',result['failure_line'],result['error'],flush=True)
 c.build('abi_'+optimization,[HERE/'layout_probe.c',ROOT/'CM7/Application/app_camera_lifecycle.c',ROOT/'CM7/Application/app_camera_lifecycle_replay.c'],'C2_LayoutRun')
 result=c.execute('abi_'+optimization,'C2_LayoutRun',0);result.pop('trace',None)
 abi.append({'optimization':optimization,**result})
(HERE/'abi_results.json').write_text(json.dumps({'cases':abi,'all_pass':all(r['pass'] for r in abi)},indent=2),encoding='utf-8')
summary={'total':len(rows),'passed':sum(r['pass'] for r in rows),'failed':sum(not r['pass'] for r in rows),'stress_transactions':2*(256*64+4096)}
(HERE/'results.json').write_text(json.dumps({'summary':summary,'cases':rows,'hardware_verified':False},indent=2),encoding='utf-8')
print(summary);raise SystemExit(bool(summary['failed']) or not all(r['pass'] for r in abi))
