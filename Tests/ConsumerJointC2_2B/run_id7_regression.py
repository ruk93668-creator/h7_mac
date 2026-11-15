"""Re-run unchanged assertions in a fresh source snapshot; retain all old FAIL records."""
from pathlib import Path
import subprocess,sys,os,io,tarfile,json,time,shutil
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/B2B6'/('original_'+str(time.time_ns()));OUT.mkdir(parents=True)
g=['git','-c','safe.directory='+ROOT.as_posix()]
b=subprocess.check_output(g+['archive','HEAD'],cwd=ROOT)
with tarfile.open(fileobj=io.BytesIO(b)) as t:t.extractall(OUT,filter='data')
# Overlay only the authorized production fix into the immutable source snapshot.
p=OUT/'CM7/Application/app_usb.c';p.write_bytes((ROOT/'CM7/Application/app_usb.c').read_bytes())

env=dict(os.environ,PYTHONIOENCODING='utf-8',PYTHONPATH=str(ROOT/'Tests/OledAsyncAcceptance/python_deps'),GIT_DIR=subprocess.check_output(g+['rev-parse','--absolute-git-dir'],cwd=ROOT).decode().strip(),GIT_WORK_TREE=str(OUT))
items=[
('c1_1','Tests/CameraLifecycleC1_1/run_tests.py','Tests/CameraLifecycleC1_1/results.json',False),
('c2_1_1','Tests/CameraLifecycleC2_1_1/run_tests.py','Tests/CameraLifecycleC2_1_1/results.json',False),
('c2a','Tests/CameraStopOwnershipR2/run_original.py','Tests/CameraStopOwnershipR2/original_results.json',True),
('r2','Tests/CameraStopOwnershipR2/run_tests.py','Tests/CameraStopOwnershipR2/results.json',True),
('b1','Tests/CameraModuleB1/run_tests.py','Tests/CameraModuleB1/results.json',True),
('b1_media','Tests/CameraModuleB1/run_media.py','Tests/CameraModuleB1/media_results.json',True),
('b1_integration','Tests/CameraModuleB1/run_integration.py','Tests/CameraModuleB1/integration_results.json',True),
('b2','Tests/UsbModuleB2/run_tests.py','Tests/UsbModuleB2/results.json',True),
('b3','Tests/StorageModuleB3/run_tests.py','Tests/StorageModuleB3/results.json',True),
('business','Tests/ConsumerIntegrationC2_2B/run_business.py','Tests/ConsumerIntegrationC2_2B/business_results.json',False),
('consumer','Tests/ConsumerIntegrationC2_2B/run_tests.py','Tests/ConsumerIntegrationC2_2B/results.json',False),
('gate','Tests/CameraLifecycleC1_1/test_regression_compare.py','Tests/CameraLifecycleC1_1/regression_gate_results.json',False),
('power','Tests/CameraPowerV1/run_tests.py','output/V1CameraPower/tests/results.json',False),
('i2c','Tests/CameraI2cV1/run_tests.py','output/V1CameraI2c1/tests/results.json',False)]
rows=[];dest=HERE/'ID7/Evidence/regression';dest.mkdir(parents=True,exist_ok=True)
for name,script,report,legacy in items:
 f=OUT/report;f.unlink(missing_ok=True)
 command=[sys.executable,str(OUT/'Tests/ConsumerIntegrationC2_2B/run_legacy.py'),script] if legacy else [sys.executable,str(OUT/script)]
 start=time.time();r=subprocess.run(command,cwd=OUT,env=env,capture_output=True)
 (dest/(name+'.log')).write_bytes(r.stdout+r.stderr)
 row={'suite':name,'exit':r.returncode,'seconds':round(time.time()-start,2),'report_present':f.exists(),'snapshot':str(OUT)}
 if f.exists():
  shutil.copy2(f,dest/(name+'.json'));d=json.loads(f.read_text(encoding='utf-8'));row['summary']=d.get('summary') if isinstance(d,dict) else {'total':len(d),'passed':sum(x['pass'] for x in d)}
 print(row,flush=True);rows.append(row);(dest/'runs.json').write_text(json.dumps(rows,indent=2))
 if name=='c1_1' and (OUT/'Tests/CameraLifecycleC1/results.json').exists():shutil.copy2(OUT/'Tests/CameraLifecycleC1/results.json',dest/'c1.json')
(HERE/'ID7/regression_runs.json').write_text(json.dumps(rows,indent=2))
