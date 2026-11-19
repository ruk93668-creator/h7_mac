"""Compare matching cases only; preserve baseline failures and reject unknown status fields."""
from pathlib import Path
import json
ROOT=Path(__file__).resolve().parents[2]
files=['UvcStreaming/results.json','UvcPayload/results.json','UvcEp1Lifecycle/results.json','UvcProbeCommit/results.json','UvcProbeCommit/cancellation_results.json','UsbCoreDecoupling/results.json','UvcLifecycle/results.json','UsbUvcDescriptors/string_results.json','UvcUsbCleanup/results.json']
def status(row):
    if isinstance(row.get('pass'),bool):return row['pass']
    if isinstance(row.get('passed'),bool):return row['passed']
    if row.get('result') in ('PASS','FAIL'):return row['result']=='PASS'
    raise ValueError('Unknown result field')
def key(row):return (row.get('optimization'),row.get('id',row.get('scenario')),row.get('name'))
rows=[]
for file in files:
    reports=[]
    for side in ('final-baseline','final'):
        path=ROOT/('output/MSC41/legacy/ep0-regression-'+side+'/Tests/'+file)
        report=json.loads(path.read_text(encoding='utf-8-sig'))
        cases=report['cases'];table={key(row):status(row) for row in cases}
        if len(table)!=len(cases):raise ValueError('Non-unique case identity: '+file)
        reports.append(table)
    old,new=reports
    rows.append(dict(file=file,baseline_cases=len(old),current_cases=len(new),baseline_failed=sum(not v for v in old.values()),current_failed=sum(not v for v in new.values()),new_failures=[str(k) for k,v in new.items() if not v and old.get(k) is True],missing_cases=[str(k) for k in old.keys()-new.keys()],added_cases=[str(k) for k in new.keys()-old.keys()]))
(ROOT/'Tests/MscReconnect/legacy_case_comparison.json').write_text(json.dumps(rows,indent=2),encoding='utf-8')
print(json.dumps(rows,indent=2))
assert all(not r['new_failures'] and not r['missing_cases'] and not r['added_cases'] for r in rows)
