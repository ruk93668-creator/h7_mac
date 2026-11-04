"""Compare actual historical failure identities with an independently executed 4d463ba snapshot."""
from pathlib import Path
import json
HERE=Path(__file__).resolve().parent
current=json.loads((HERE/'regression_summary.json').read_text(encoding='utf-8'))
baseline_path=HERE/'baseline/regression_summary.json'
if not baseline_path.exists():baseline_path=HERE/'baseline_summary.json'
baseline=json.loads(baseline_path.read_text(encoding='utf-8'))
prior_path=HERE/'baseline_comparison.json'
prior=json.loads(prior_path.read_text(encoding='utf-8')) if prior_path.exists() else {}
prior_by_name={x['suite']:x for x in prior.get('suites',[])}
def summary(row):
    s=row.get('summary',{});n=s.get('total',s.get('executions',0));p=s.get('passed',s.get('pass',0))
    return {'executed':n,'passed':p,'failed':s.get('failed',s.get('fail',s.get('failures',n-p))),'not_executed':row.get('not_executed',0)}
def failures(path):
    d=json.loads(path.read_text(encoding='utf-8'));cases=d if isinstance(d,list) else d.get('cases',[]);result=[]
    for i,c in enumerate(cases):
        passed=c.get('pass',c.get('passed',c.get('result')=='PASS'))
        if not passed:
            state=c.get('state',c.get('actual',{}));state=state if isinstance(state,dict) else {}
            result.append({'id':c.get('id',i),'name':c.get('name',''),'line':c.get('failed_line',c.get('failure_line',c.get('fail_line',state.get('fail_line'))))})
    return result
rows=[]
for now,old in zip(current,baseline):
    assert now['suite']==old['suite']
    item={'suite':now['suite'],'baseline':summary(old),'current':summary(now)}
    cp=HERE/(now['suite']+'_results.json');bp=HERE/'baseline'/(old['suite']+'_results.json')
    if cp.exists() and (bp.exists() or now['suite'] in prior_by_name):
        a=failures(cp)
        b=failures(bp) if bp.exists() else prior_by_name[now['suite']]['baseline_failures']
        item.update(failure_set_identical=a==b,current_failures=a,baseline_failures=b)
    else:
        item['note']='Historical API/fixture does not compile; no case counted as executed.'
    rows.append(item)
totals={}
for label in ['baseline','current']:
    totals[label]={k:sum(x[label][k] for x in rows) for k in ['executed','passed','failed','not_executed']}
record={'baseline':'4d463ba5506263358331182bd07f3e6c09902fb9','totals':totals,'suites':rows,
        'all_executed_failure_sets_identical':all(x.get('failure_set_identical',True) for x in rows),
        'new_fixture_incompatibilities':['VideoConfig','OldBridge','Native01','Native01Usb']}
(HERE/'baseline_comparison.json').write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(totals,indent=2));print('Unchanged failure sets:',record['all_executed_failure_sets_identical'])
raise SystemExit(not record['all_executed_failure_sets_identical'])
