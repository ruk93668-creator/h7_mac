"""Compare original case IDs/results without rewriting any assertions."""
from pathlib import Path
import json,hashlib,subprocess
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Tests/UsbBulkIn/reports'
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def cases(row):
 found={}
 for artifact in row['produced_reports']:
  def visit(node,path):
   if isinstance(node,dict):
    if ('id' in node or 'scenario' in node) and any(k in node for k in ['pass','passed','result']):
     identity=str(node.get('id',node.get('scenario')))+'/'+str(node.get('optimization',''))+'/'+str(node.get('name',''))
     status=node.get('pass',node.get('passed',node.get('result')))
     if isinstance(status,str):status=status=='PASS'
     if isinstance(status,bool):
      found[artifact['path']+'#'+identity]=dict(passed=status,checks=node.get('checks',node.get('assertions')),failure_line=node.get('failed_line',node.get('fail_line')),error=node.get('error'),actual=node.get('failure_actual',node.get('actual')),expected=node.get('failure_expected',node.get('expected')))
    for k,v in node.items():visit(v,path+'/'+k)
   elif isinstance(node,list):
    for i,v in enumerate(node):visit(v,path+'/'+str(i))
  visit(artifact['content'],'')
 return found
before=load(ROOT/'Tests/EndpointLifecycle/reports/baseline-regression-p1b-before.json')
after=load(OUT/'regression-p1b04.json');assert len(before['rows'])==len(after['rows'])==17
bymap={r['suite']:r for r in before['rows']};result=[]
for r in after['rows']:
 b=bymap[r['suite']];a_cases=cases(r);b_cases=cases(b);changes=[]
 for name in sorted(set(a_cases)|set(b_cases)):
  x=b_cases.get(name);y=a_cases.get(name)
  changes.append(dict(case=name,baseline=x,current=y,verdict='NOT_RUN_OR_MISSING' if y is None else 'NEW_CASE' if x is None else 'NEW_FAILURE' if x['passed'] and not y['passed'] else 'FIXED' if not x['passed'] and y['passed'] else 'UNCHANGED'))
 result.append(dict(suite=r['suite'],baseline_exit=b['exit'],current_exit=r['exit'],baseline_case_count=len(b_cases),current_case_count=len(a_cases),case_comparison=changes,new_failures=[x['case'] for x in changes if x['verdict']=='NEW_FAILURE'],missing=[x['case'] for x in changes if x['verdict']=='NOT_RUN_OR_MISSING'],baseline_log=b['log'],current_log=r['log']))
adapted=['Tests/UsbCoreDecoupling/core_test.c','Tests/UsbCoreDecoupling/endpoint_lifecycle_test.c','Tests/Ep0IrqRace/race_test.c']
for name in adapted:
 old=subprocess.check_output(['git','show','5a27af6:'+name],cwd=ROOT);assert (ROOT/name).read_bytes().replace(b'.queued_bytes',b'.offset')==old
name='Tests/MscEnumeration/hal_fixture.c';assert (ROOT/name).read_bytes().startswith(subprocess.check_output(['git','show','5a27af6:'+name],cwd=ROOT))
report=dict(baseline='5a27af621c219ead2bc680858efd4aeb17e110a4',unchanged_assertions_verified=True,suites=result,entry_summary=dict(baseline_pass=sum(x['exit']==0 for x in before['rows']),baseline_fail=sum(x['exit']!=0 for x in before['rows']),current_pass=sum(x['exit']==0 for x in after['rows']),current_fail=sum(x['exit']!=0 for x in after['rows'])),new_case_failures=[dict(suite=r['suite'],case=x) for r in result for x in r['new_failures']],sources={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in ['CM7/BSP/bsp_usb.c','CM7/BSP/bsp_usb.h','CM7/Platform/usb_device.c','CM7/Platform/usb_device.h']})
(OUT/'case_comparison.json').write_bytes(json.dumps(report,ensure_ascii=False,indent=2).encode('utf-8'))
print(json.dumps(dict(entry_summary=report['entry_summary'],new_case_failures=report['new_case_failures']),ensure_ascii=False))
