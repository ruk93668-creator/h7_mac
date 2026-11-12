"""Run unchanged C1/C1.1 scripts on an isolated candidate snapshot; preserve original output."""
from pathlib import Path
import subprocess,sys,json,os,io,tarfile,time
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
BASE='182c84e7334f0a62d96f1d3109b0d7fb8efb8158'
OUT=ROOT/'output/CameraLifecycleC2_1_1'/('legacy_'+str(time.time_ns()));OUT.mkdir(parents=True)
archive=subprocess.check_output(['git','-c','safe.directory='+ROOT.as_posix(),'archive','--format=tar',BASE],cwd=ROOT)
with tarfile.open(fileobj=io.BytesIO(archive)) as t:t.extractall(OUT,filter='data')
for rel in ['CM7/Application/app_camera_lifecycle_replay.c','CM7/Application/app_camera_lifecycle_replay.h','MDK-ARM/h7_mac.uvprojx']:
 (OUT/rel).write_bytes((ROOT/rel).read_bytes())
env=dict(os.environ,PYTHONIOENCODING='utf-8',PYTHONPATH=str(ROOT/'Tests/OledAsyncAcceptance/python_deps'))
results=[]
for script in ['Tests/CameraLifecycleC1_1/run_tests.py','Tests/CameraLifecycleC1_1/test_regression_compare.py']:
 r=subprocess.run([sys.executable,str(OUT/script)],cwd=OUT,env=env,capture_output=True)
 log=OUT/(Path(script).stem+'.log');log.write_bytes(r.stdout+r.stderr)
 print(script,'exit',r.returncode,flush=True);results.append({'script':script,'exit':r.returncode,'log':str(log)})
 if r.returncode:print((r.stdout+r.stderr).decode('utf-8',errors='replace')[-3000:]);raise SystemExit(r.returncode)
for src,name in [('Tests/CameraLifecycleC1/results.json','original_c1_results.json'),('Tests/CameraLifecycleC1_1/results.json','original_c1_1_results.json'),('Tests/CameraLifecycleC1_1/regression_gate_results.json','gate_results.json')]:
 (HERE/name).write_bytes((OUT/src).read_bytes())
(HERE/'legacy_results.json').write_text(json.dumps({'baseline':BASE,'snapshot':str(OUT),'scripts':results,'original_assertions_changed':False},indent=2),encoding='utf-8')
