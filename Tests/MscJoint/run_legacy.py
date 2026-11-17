"""Run existing UVC/Core suites in an isolated workspace; never rewrite historical reports."""
from pathlib import Path
import shutil, subprocess, sys, os, json
ROOT=Path(__file__).resolve().parents[2]; baseline='--baseline' in sys.argv
label='final-baseline' if baseline else 'final'
WORK=ROOT/('output/MSC24/ep0-regression-'+label)
WORK.mkdir(parents=True,exist_ok=True)
for folder in ['CM7','Common','Drivers','Middlewares']:
 shutil.copytree(ROOT/folder,WORK/folder,dirs_exist_ok=True)
for source in (ROOT/'Tests').rglob('*'):
 if source.is_file() and source.suffix in ['.py','.c','.h','.sct','.json'] and 'python_deps' not in source.parts:
  dest=WORK/source.relative_to(ROOT);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,dest)
if baseline:
 for name in ['CM7/Service/usb_mode.c','CM7/Service/usb_mode.h','CM7/Application/app_main.c','CM7/Application/video_config.c','CM7/Platform/usb_device.c','CM7/Platform/usb_device.h']:
  raw=subprocess.check_output(['git','-c','safe.directory='+ROOT.as_posix(),'show','14f44ca678647421d3519f192d441724178a7882:'+name],cwd=ROOT)
  (WORK/name).write_bytes(raw)
env=dict(os.environ,PYTHONPATH=str(ROOT/'output/msc1-deps'),PYTHONDONTWRITEBYTECODE='1',PYTHONIOENCODING='utf-8')
suites=['UvcStreaming/run_tests.py','UvcPayload/run_tests.py','UvcEp1Lifecycle/run_tests.py','UvcProbeCommit/run_tests.py','UvcProbeCommit/run_cancellation_tests.py','UsbCoreDecoupling/run_tests.py','Ep0IrqRace/run_race.py','UvcLifecycle/run_tests.py','UsbUvcDescriptors/run_tests.py','UvcUsbCleanup/run_tests.py']
rows=[]
for suite in suites:
 r=subprocess.run([sys.executable,str(WORK/'Tests'/suite)],cwd=WORK,env=env,capture_output=True)
 log=(r.stdout+r.stderr).decode('utf-8',errors='replace')
 (ROOT/'output/MSC24'/(label+'_'+suite.replace('/','_')+'.log')).write_text(log,encoding='utf-8')
 rows.append(dict(suite=suite,exit=r.returncode,passed=r.returncode==0,tail=log[-1200:]))
 print(suite,'PASS' if r.returncode==0 else 'FAIL',log[-350:],flush=True)
 (ROOT/('Tests/MscJoint/'+label+'_regression_results.json')).write_text(json.dumps(rows,indent=2),encoding='utf-8')
raise SystemExit(not all(r['passed'] for r in rows))
