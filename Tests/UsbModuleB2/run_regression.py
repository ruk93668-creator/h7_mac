"""Run unchanged historical tests on isolated baseline/current source snapshots."""
from pathlib import Path
import subprocess,sys,os,json,io,tarfile,concurrent.futures,hashlib,time
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/UsbModuleB2/regression';OUT.mkdir(parents=True,exist_ok=True)
BASE='8bfa9bc4316c0faaceef65bc90fbe84d03eae55f'
SUITES=[
 ('B1Behavior','Tests/CameraModuleB1/run_tests.py',[],'results.json'),
 ('B1BridgeStorage','Tests/CameraModuleB1/run_integration.py',[],'integration_results.json'),
 ('B1Media','Tests/CameraModuleB1/run_media.py',[],'media_results.json'),
 ('CameraApp','Tests/CameraAppInit/run_tests.py',[],'results.json'),
 ('CameraContext','Tests/CameraContextRefactor/run_tests.py',[],'results.json'),
 ('CameraStream','Tests/CameraStreamState/run_tests.py',[],'results.json'),
 ('CameraDeviceLifecycle','Tests/Ov5640Lifecycle/run_tests.py',[],'results.json'),
 ('JpegMedia','Tests/CameraJpegStage03A/run_media.py',['media_test','--count','28'],'media_test_results.json'),
 ('JpegDevice','Tests/CameraJpegStage03A/run_device.py',[],'device_results.json'),
 ('ContinuousMedia','Tests/NativeRuntimeStage02/run_arm.py',['phase02a','--count','10'],'phase02a_results.json'),
 ('Bridge','Tests/NativeRuntimeStage02/run_arm.py',['phase02c','--count','28'],'phase02c_results.json'),
 ('NativeIntegrated','Tests/NativeRuntimeStage02/run_native.py',['phase02d','--count','33'],'phase02d_results.json'),
 ('NativeTest','Tests/NativeRuntimeStage02/run_native.py',['phase02test','--count','10'],'phase02test_results.json'),
 ('Core','Tests/UsbCoreDecoupling/run_tests.py',[],'results.json'),
 ('EP0IRQ','Tests/Ep0IrqRace/run_race.py',[],'results.json'),
 ('Probe','Tests/UvcProbeCommit/run_tests.py',[],'results.json'),
 ('Cancellation','Tests/UvcProbeCommit/run_cancellation_tests.py',[],'cancellation_results.json'),
 ('Payload','Tests/UvcPayload/run_tests.py',[],'results.json'),
 ('UvcLifecycle','Tests/UvcLifecycle/run_tests.py',[],'results.json'),
 ('EP1','Tests/UvcEp1Lifecycle/run_tests.py',[],'results.json'),
 ('Streaming','Tests/UvcStreaming/run_tests.py',[],'results.json'),
 ('Descriptors','Tests/UsbUvcDescriptors/run_tests.py',[],'results.json'),
 ('Strings','Tests/UsbUvcDescriptors/run_string_tests.py',[],'string_results.json'),
 ('VideoConfig','Tests/VideoConfig/run_tests.py',[],'results.json'),
 ('IpcStatus','Tests/IpcStatus/run_tests.py',[],'results.json'),
]
tracked=subprocess.check_output(['git','-c','safe.directory='+ROOT.as_posix(),'ls-files','-z'],cwd=ROOT).decode().split('\0')
actual={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in tracked if p and (ROOT/p).is_file()}
archive=subprocess.check_output(['git','-c','safe.directory='+ROOT.as_posix(),'archive','--format=tar',BASE],cwd=ROOT)
for variant in ['baseline','current']:
 dest=OUT/variant;dest.mkdir(exist_ok=True)
 if not (dest/'CM7/Application/app_camera.c').exists():
  with tarfile.open(fileobj=io.BytesIO(archive)) as t:t.extractall(dest,filter='data')
 # Historical tests stay unchanged; overlay only B2 production files and CM7 registration.
 if variant=='current':
  for rel in ['CM7/Application/Tasks/usb_task.c','CM7/Application/app_usb.c','CM7/Application/app_usb.h','MDK-ARM/h7_mac.uvprojx']:
   (dest/rel).write_bytes((ROOT/rel).read_bytes())
env=dict(os.environ,PYTHONIOENCODING='utf-8',PYTHONPATH=str(ROOT/'Tests/OledAsyncAcceptance/python_deps'))
def run_variant(variant):
 dest=OUT/variant;rows=[]
 for name,script,args,report in SUITES:
  result_path=(dest/script).parent/report
  result_path.unlink(missing_ok=True)
  start=time.monotonic()
  try:
   p=subprocess.run([sys.executable,str(dest/script),*args],cwd=dest,env=env,capture_output=True,timeout=180)
   text=(p.stdout+p.stderr).decode('utf-8',errors='replace');exit=p.returncode
  except subprocess.TimeoutExpired as ex:
   text=((ex.stdout or b'')+(ex.stderr or b'')).decode('utf-8',errors='replace');exit='TIMEOUT'
  (OUT/(variant+'_'+name+'.log')).write_text(text,encoding='utf-8')
  row={'suite':name,'exit':exit,'elapsed_s':round(time.monotonic()-start,2)}
  if result_path.exists():
   raw=json.loads(result_path.read_text(encoding='utf-8-sig'))
   (OUT/(variant+'_'+name+'.json')).write_text(json.dumps(raw,ensure_ascii=False,indent=2),encoding='utf-8')
   cases=raw if isinstance(raw,list) else raw.get('cases',raw.get('tests',[]))
   row['summary']=raw.get('summary',{}) if isinstance(raw,dict) else {}
   row['cases']=[]
   for i,c in enumerate(cases):
    val=c.get('pass',c.get('passed',c.get('result',False)))
    ok=val is True or val=='PASS'
    row['cases'].append({'id':c.get('id',i),'pass':ok,'failure':c.get('failure_line',c.get('fail_line'))})
   if not row['summary'] and row['cases']:
    row['summary']={'total':len(cases),'passed':sum(x['pass'] for x in row['cases']),'failed':sum(not x['pass'] for x in row['cases'])}
  if exit!=0:row['diagnostic_tail']=text[-2500:]
  rows.append(row)
  (OUT/(variant+'_summary.json')).write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
  print(variant,name,row.get('summary',{}),'exit',exit,flush=True)
 return rows
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
 fs={n:pool.submit(run_variant,n) for n in ['baseline','current']}
 results={n:f.result() for n,f in fs.items()}
comparison=[]
for a,b in zip(results['baseline'],results['current']):
 comparison.append({'suite':a['suite'],'baseline_exit':a['exit'],'current_exit':b['exit'],'baseline_summary':a.get('summary',{}),'current_summary':b.get('summary',{}),'case_results_equal':a.get('cases')==b.get('cases'),'baseline_diagnostic':a.get('diagnostic_tail',''),'current_diagnostic':b.get('diagnostic_tail',''),'new_failure':(a['exit']==0 and b['exit']!=0) or any(x['pass'] and not y['pass'] for x,y in zip(a.get('cases',[]),b.get('cases',[])))})
changed=[p for p,h in actual.items() if not (ROOT/p).exists() or hashlib.sha256((ROOT/p).read_bytes()).hexdigest()!=h]
result={'baseline':BASE,'comparison':comparison,'new_failures':sum(x['new_failure'] for x in comparison),'actual_workspace_changed_by_regression':changed,'historical_tests_modified':False,'note':'Historical suites and B1 Camera/Bridge/Storage/Media run unchanged; unavailable fixtures and their first diagnostic remain visible.'}
(HERE/'regression_results.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
raise SystemExit(bool(result['new_failures'] or changed))
