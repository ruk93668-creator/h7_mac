"""Run independent software suites; HAL/SD mocks do not imply board acceptance."""
from pathlib import Path
import concurrent.futures, subprocess, sys, json, os
ROOT=Path(__file__).resolve().parents[2]
SUITES=[('MscDeferredStatus/run_tests.py',[]),('MscProtocol/run_tests.py',[]),('MscBlockAdapter/run_tests.py',[]),('MscSdIntegration/run_tests.py',[]),('MscSdIntegration/run_tests.py',['--write']),('MscSdIntegration/run_gate.py',[]),('MscJoint/run_tests.py',[]),('MscJoint/run_test_compat.py',[]),('MscJoint/run_test_compat.py',['--baseline'])]
OUT=ROOT/'output/MSC24/final-tests';OUT.mkdir(parents=True,exist_ok=True)
def run(item):
    name,args=item
    result=subprocess.run([sys.executable,str(ROOT/'Tests'/name),*args],cwd=ROOT,capture_output=True,env=dict(os.environ,PYTHONUTF8='1'))
    log=(result.stdout+result.stderr).decode('utf-8',errors='replace')
    (OUT/(name.replace('/','_')+('_'+args[0].lstrip('-') if args else '')+'.log')).write_text(log,encoding='utf-8')
    return dict(suite=name,args=args,exit=result.returncode,passed=result.returncode==0)
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    rows=list(pool.map(run,SUITES))
(ROOT/'Tests/MscJoint/software_results.json').write_text(json.dumps(rows,indent=2),encoding='utf-8')
for row in rows:print(row)
raise SystemExit(not all(row['passed'] for row in rows))
