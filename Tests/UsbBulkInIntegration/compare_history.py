"""Compare per-case outcomes to both original and P1-B baselines; no assertion edits."""
from pathlib import Path
import json, hashlib, subprocess
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'Tests/UsbBulkInIntegration/reports'
ns={"__file__":str(ROOT/'Tests/UsbBulkIn/compare_results.py')}
exec((ROOT/'Tests/UsbBulkIn/compare_results.py').read_text(encoding='utf-8').split('before=load(')[0],ns)
cases=ns['cases']
def load(p):return json.loads((ROOT/p).read_text(encoding='utf-8'))
after=load('Tests/UsbBulkIn/reports/regression-p1b1-final04.json')
assert len(after['rows'])==17 and not after['original_changed']
comparisons=[]
for label,path in [('original','Tests/EndpointLifecycle/reports/baseline-regression-p1b-before.json'),('5113000','Tests/UsbBulkIn/reports/regression-p1b04.json')]:
 baseline=load(path);prior={x['suite']:x for x in baseline['rows']};rows=[]
 for row in after['rows']:
  old=cases(prior[row['suite']]);new=cases(row);detail=[]
  for key in sorted(set(old)|set(new)):
   a=old.get(key);b=new.get(key)
   verdict='MISSING' if b is None else 'NEW_CASE' if a is None else 'NEW_FAILURE' if a['passed'] and not b['passed'] else 'FIXED' if not a['passed'] and b['passed'] else 'UNCHANGED'
   detail.append(dict(case=key,baseline=a,current=b,verdict=verdict))
  rows.append(dict(suite=row['suite'],baseline_exit=prior[row['suite']]['exit'],current_exit=row['exit'],cases=detail,new_failures=[d['case'] for d in detail if d['verdict']=='NEW_FAILURE']))
 comparisons.append(dict(baseline=label,suites=rows))
# Original expectations remain byte-identical to start of this task.
assertions=['Tests/UsbCoreDecoupling/core_test.c','Tests/UsbCoreDecoupling/endpoint_lifecycle_test.c','Tests/Ep0IrqRace/race_test.c','Tests/UsbBulkIn/bulk_in_test.c','Tests/UsbBulkIn/run_tests.py']
for name in assertions:assert (ROOT/name).read_bytes()==subprocess.check_output(['git','show','5113000:'+name],cwd=ROOT),name
# EP0 runner emitted byte-identical results, so snapshot driver intentionally omitted them.
ep0_name='Tests/Ep0IrqRace/results.json'
ep0_old=json.loads(subprocess.check_output(['git','show','5113000:'+ep0_name],cwd=ROOT).decode('utf-8'))
ep0_new=json.loads((Path(after['snapshot'])/ep0_name).read_text(encoding='utf-8'))
assert len(ep0_new)==4 and all(x['pass'] for x in ep0_new)
ep0_comparison=[dict(scenario=n['scenario'],baseline=next(x for x in ep0_old if x['scenario']==n['scenario']),current=n) for n in ep0_new]
report=dict(ep0_unchanged_report_cases=ep0_comparison,baseline='5113000227e8d3d3a9a460b4585993e28096d0cc',comparisons=comparisons,old_assertions_unchanged=True,suite_count=17,entry_success=sum(x['exit']==0 for x in after['rows']),entry_failed=sum(x['exit']!=0 for x in after['rows']),source_hashes={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in assertions})
(OUT/'history_comparison.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(dict(entries=[report['entry_success'],report['entry_failed']],new_failures={r['baseline']:[dict(suite=s['suite'],cases=s['new_failures']) for s in r['suites'] if s['new_failures']] for r in comparisons}),ensure_ascii=False))
