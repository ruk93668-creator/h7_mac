"""Run unchanged historical tests on isolated baseline/current source snapshots."""
from pathlib import Path
import subprocess,sys,os,json,io,tarfile,concurrent.futures,hashlib,time,re,argparse
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'Tests/CameraLifecycleC1'))
from regression_compare import normalize_report, compare_all
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/CameraStopOwnershipR2/regression'/str(time.time_ns());OUT.mkdir(parents=True,exist_ok=True)
parser=argparse.ArgumentParser()
parser.add_argument('--baseline',default='fa9ce5d')
args=parser.parse_args()
BASE=args.baseline
SUITES=[
 ('B3Behavior','Tests/StorageModuleB3/run_tests.py',[],'results.json'),
 ('B2Behavior','Tests/UsbModuleB2/run_tests.py',[],'results.json'),
 ('B2Integration','Tests/UsbModuleB2/run_integration.py',[],'integration_results.json'),
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
 with tarfile.open(fileobj=io.BytesIO(archive)) as t:t.extractall(dest,filter='data')
 # Historical tests stay unchanged; overlay only the C1 protocol, message additions and CM7 source registration.
 if variant=='current':
  for rel in ['CM7/Application/app_messages.c','CM7/Application/app_messages.h','CM7/Application/app_camera_lifecycle.c','CM7/Application/app_camera_lifecycle.h','CM7/Application/app_camera_lifecycle_replay.c','CM7/Application/app_camera_lifecycle_replay.h','MDK-ARM/h7_mac.uvprojx','CM7/Application/app_camera.c','CM7/Application/app_camera.h','CM7/Application/app_camera_qualification.c','CM7/Application/app_camera_qualification.h','CM7/Application/app_camera_qualification_internal.h','CM7/Application/Tasks/camera_task.c','CM7/Media/Inc/camera_capture.h','CM7/Media/Src/camera_capture.c','Tests/CameraModuleB1/behavior_test.c']:
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
   row.update(normalize_report(raw))
   row['report_present']=True
  if not result_path.exists():
   row.update({'cases':[],'summary':{},'schema_errors':[],'report_present':False})
  if exit!=0:
   row['diagnostic_tail']=text[-2500:]
   normalized=text
   for path in [str(dest),dest.as_posix(),str(dest).replace('\\','\\\\')]:normalized=normalized.replace(path,'<SNAPSHOT>')
   row['diagnostic_fingerprint']='\n'.join(line.strip() for line in normalized.splitlines() if re.search(r'Error:|fatal error|Undefined symbol|cannot open source',line))

  rows.append(row)
  (OUT/(variant+'_summary.json')).write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
  print(variant,name,row.get('summary',{}),'exit',exit,flush=True)
 return rows
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
 fs={n:pool.submit(run_variant,n) for n in ['baseline','current']}
 results={n:f.result() for n,f in fs.items()}
comparison_result=compare_all(results['baseline'],results['current'])
changed=[p for p,h in actual.items() if not (ROOT/p).exists() or hashlib.sha256((ROOT/p).read_bytes()).hexdigest()!=h]
result={'baseline':BASE,**comparison_result,'artifact_root':str(OUT),'actual_workspace_changed_by_regression':changed,'historical_tests_modified':True,'historical_assertions_modified':False,'note':'Original assertions retained; B1 dependency fixture adds only the readonly facts stub. Unavailable fixtures and original trace failures remain visible.'}
(HERE/'regression_results.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
raise SystemExit(bool(result['new_failures'] or changed))
