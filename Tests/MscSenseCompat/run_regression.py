"""Run historical suites without overwriting their committed result records."""
from pathlib import Path
import subprocess,sys,json,os
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/MSC24/msc21/regression';OUT.mkdir(parents=True,exist_ok=True)
git=['git','-c','safe.directory='+ROOT.as_posix()]
paths=subprocess.check_output(git+['ls-files','Tests/*.json'],cwd=ROOT).decode().splitlines()
saved={name:(ROOT/name).read_bytes() for name in paths if (ROOT/name).is_file()}
rows=[]
try:
    for script in ['Tests/MscJoint/run_all.py','Tests/MscJoint/run_legacy.py']:
        result=subprocess.run([sys.executable,str(ROOT/script)],cwd=ROOT,capture_output=True,env=dict(os.environ,PYTHONUTF8='1'))
        log=(result.stdout+result.stderr).decode('utf-8',errors='replace');(OUT/(Path(script).stem+'.log')).write_text(log,encoding='utf-8')
        rows.append(dict(script=script,exit=result.returncode))
    current=json.loads((ROOT/'Tests/MscJoint/software_results.json').read_text())
    legacy=json.loads((ROOT/'Tests/MscJoint/final_regression_results.json').read_text())
    expected={row['suite']:row['exit'] for row in json.loads((ROOT/'Tests/MscDeferredStatus/baseline_regression_results.json').read_text(encoding='utf-8'))}
    comparisons=[]
    for suite in legacy:
        name=suite['suite'].replace('/','_')+'.log'
        now=(ROOT/'output/MSC24'/('final_'+name)).read_text(encoding='utf-8')
        before=(ROOT/'output/MSC24'/('baseline_'+name)).read_text(encoding='utf-8')
        signature=lambda s:[line for line in s.splitlines() if ' PASS ' in line or ' FAIL ' in line or line.startswith(('TOTAL',"{'total","{'unique_cases"))]
        comparisons.append(dict(suite=suite['suite'],same_assertion_outcomes=signature(now)==signature(before),same_exit=suite['exit']==expected[suite['suite']]))
    report=dict(commands=rows,msc_suites=current,uvc_legacy=legacy,baseline_comparison=comparisons)
    (HERE/'regression_results.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    assert all(row['passed'] for row in current)
    assert all(row['same_assertion_outcomes'] and row['same_exit'] for row in comparisons)
    print('MSC suites: all passed; legacy suites: all 10 match approval baseline',flush=True)
finally:
    for name,data in saved.items():
        if (ROOT/name).read_bytes()!=data:(ROOT/name).write_bytes(data)
