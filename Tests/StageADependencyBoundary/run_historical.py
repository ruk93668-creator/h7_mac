import os,sys,json,subprocess,hashlib,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]; OUT=Path(os.environ['TEMP'])/'h7_stageA_48afa27'; OUT.mkdir(exist_ok=True)
SUITES=['IpcV2/run_tests.py','IpcRuntime/run_tests.py','IpcRuntime/run_task_tests.py','IpcStatus/run_tests.py','IpcStatus/run_kernel_tests.py','OledMonitor/run_tests.py','OledAsyncAcceptance/run_tests.py','UvcPayload/run_tests.py','UsbCoreDecoupling/run_tests.py','Ep0IrqRace/run_race.py','UvcLifecycle/run_tests.py','UvcEp1Lifecycle/run_tests.py','UvcProbeCommit/run_tests.py','UvcProbeCommit/run_cancellation_tests.py','UvcStreaming/run_tests.py','UsbUvcDescriptors/run_tests.py','UsbUvcDescriptors/run_string_tests.py','UvcUsbCleanup/run_tests.py','VideoConfig/run_tests.py','CameraStreamState/run_tests.py','BspMediaContinuous/run_tests.py','DcmiFramePair/run_tests.py','NativeRuntimeStage01/run_tests.py','NativeRuntimeStage02/run_tests.py','Ov5640Cleanup/run_tests.py']
phase=sys.argv[1]; rows=[]; env=dict(os.environ,PYTHONPATH=str(OUT/'deps'),PYTHONIOENCODING='utf-8',GIT_CONFIG_COUNT='1',GIT_CONFIG_KEY_0='safe.directory',GIT_CONFIG_VALUE_0=str(ROOT).replace('\\','/'))
tracked=subprocess.check_output(['git','-c','safe.directory='+str(ROOT),'ls-files','-z','Tests'],cwd=ROOT).decode().split('\0'); saved={p:(ROOT/p).read_bytes() for p in tracked if p}
for script in SUITES:
 p=ROOT/'Tests'/script
 if not p.exists(): continue
 key='task_results.json' if 'task_tests' in script or 'kernel_tests' in script else 'cancellation_results.json' if 'cancellation' in script else 'string_results.json' if 'string_tests' in script else 'results.json'
 primary=p.parent/key;primary.unlink(missing_ok=True)
 start=time.time(); r=subprocess.run([sys.executable,str(p)],cwd=ROOT,env=env,capture_output=True,timeout=600)
 log=(r.stdout+r.stderr).decode('utf-8',errors='replace'); name=script.replace('/','_'); (OUT/(phase+'_'+name+'.log')).write_text(log,encoding='utf-8')
 results={}
 for q in p.parent.glob('*results.json'):
  if q==primary or str(q.relative_to(ROOT)) not in saved or q.read_bytes()!=saved[str(q.relative_to(ROOT))]:
   try:results[q.name]=json.loads(q.read_text(encoding='utf-8'))
   except Exception:pass
 row=dict(script=script,exit=r.returncode,seconds=round(time.time()-start,1),results=results,tail=log[-1200:]);rows.append(row)
 for name,raw in saved.items():
  q=ROOT/name
  if not q.exists() or q.read_bytes()!=raw:q.write_bytes(raw)
 (OUT/(phase+'_regression.json')).write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
 print(phase,script,'exit',r.returncode,round(time.time()-start,1),flush=True)
print('DONE',phase,len(rows),flush=True)
