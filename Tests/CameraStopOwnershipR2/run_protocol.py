"""Execute original prerequisite scripts unchanged in an isolated candidate snapshot."""
from pathlib import Path
import io,os,tarfile,subprocess,sys,json,time,hashlib
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/CameraStopOwnershipR2'/('protocol_'+str(time.time_ns()));OUT.mkdir(parents=True)
g=['git','-c','safe.directory='+ROOT.as_posix()]
raw=subprocess.check_output(g+['archive','--format=tar','HEAD'],cwd=ROOT)
with tarfile.open(fileobj=io.BytesIO(raw)) as t:t.extractall(OUT,filter='data')
for rel in ['CM7/Application/app_camera.c','CM7/Application/app_camera.h','CM7/Application/app_camera_qualification.c','CM7/Application/app_camera_qualification.h','CM7/Application/app_camera_qualification_internal.h','MDK-ARM/h7_mac.uvprojx','CM7/Application/Tasks/camera_task.c','CM7/Media/Inc/camera_capture.h','CM7/Media/Src/camera_capture.c']:
 (OUT/rel).write_bytes((ROOT/rel).read_bytes())
protected=['CM7/Application/app_camera_lifecycle.c','CM7/Application/app_camera_lifecycle.h','CM7/Application/app_camera_lifecycle_replay.c','CM7/Application/app_camera_lifecycle_replay.h']
for rel in protected:
 assert (ROOT/rel).read_bytes()==subprocess.check_output(g+['show','c6d2bed:'+rel],cwd=ROOT)
env=dict(os.environ,PYTHONIOENCODING='utf-8',PYTHONPATH=str(ROOT/'Tests/OledAsyncAcceptance/python_deps'))
# Remove inherited reports before executing so stale committed PASS cannot be mistaken for fresh results.
report_paths={
 'c2_1_1':OUT/'Tests/CameraLifecycleC2_1_1/results.json',
 'abi':OUT/'Tests/CameraLifecycleC2_1_1/abi_results.json',
 'c1':OUT/'Tests/CameraLifecycleC1/results.json',
 'c1_1':OUT/'Tests/CameraLifecycleC1_1/results.json',
 'gate':OUT/'Tests/CameraLifecycleC1_1/regression_gate_results.json'}
for path in report_paths.values():path.unlink(missing_ok=True)
rows=[]
for script in ['Tests/CameraLifecycleC2_1_1/run_tests.py','Tests/CameraLifecycleC1_1/run_tests.py','Tests/CameraLifecycleC1_1/test_regression_compare.py']:
 r=subprocess.run([sys.executable,str(OUT/script)],cwd=OUT,env=env,capture_output=True)
 log=OUT/(Path(script).parent.name+'_'+Path(script).stem+'.log');log.write_bytes(r.stdout+r.stderr)
 print(script,'exit',r.returncode,flush=True)
 rows.append({'script':script,'exit':r.returncode,'log':str(log)})
 if r.returncode:print((r.stdout+r.stderr).decode('utf-8',errors='replace')[-3500:]);break
reports={}
for name,f in report_paths.items():
 if f.exists():reports[name]=json.loads(f.read_text(encoding='utf-8'))
result={'snapshot':str(OUT),'scripts':rows,'reports':reports,'original_protocol_sources_identical_to_c6d2bed':True,'original_test_assertions_modified':False,'hardware_verified':False}
(HERE/'protocol_results.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print({name:r.get('summary',r.get('all_pass',r.get('total'))) for name,r in reports.items()},flush=True)
raise SystemExit(any(r['exit'] for r in rows) or len(reports)!=5)
