"""Compare original case IDs/results without rewriting any assertions."""
from pathlib import Path
import json,hashlib,subprocess
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Tests/UsbBulkOutLifecycle/reports'
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

import argparse
ap=argparse.ArgumentParser();ap.add_argument('--tag',default='c1-final03');arg=ap.parse_args()
baseline=load(ROOT/'Tests/UsbBulkOut/reports/regression-p1c-c0.json')
current=load(OUT/('regression-'+arg.tag+'.json'))
assert len(baseline['rows'])==len(current['rows'])==17
bm={r['suite']:r for r in baseline['rows']};rows=[]
for r in current['rows']:
 b=bm[r['suite']]
 if r['suite']=='Ep0IrqRace/run_race.py':
  n='Tests/Ep0IrqRace/results.json'
  b=dict(b,produced_reports=[dict(path=n,content=json.loads(subprocess.check_output(['git','show','3305d4a:'+n],cwd=ROOT)))])
  r=dict(r,produced_reports=[dict(path=n,content=load(Path(current['snapshot'])/n))])
 bc,ac=cases(b),cases(r);entries=[]
 for name in sorted(set(bc)|set(ac)):
  x,y=bc.get(name),ac.get(name)
  verdict='MISSING' if y is None else 'NEW_CASE' if x is None else 'NEW_FAILURE' if x['passed'] and not y['passed'] else 'FIXED' if not x['passed'] and y['passed'] else 'UNCHANGED'
  entries.append(dict(case=name,baseline=x,current=y,verdict=verdict,detail_equal=x==y))
 rows.append(dict(suite=r['suite'],baseline_exit=b['exit'],current_exit=r['exit'],baseline_cases=len(bc),current_cases=len(ac),passed=sum(x['passed'] for x in ac.values()),failed=sum(not x['passed'] for x in ac.values()),case_comparison=entries))
# Verify ALL pre-existing test files byte-for-byte against the authorized baseline.
basefiles=subprocess.check_output(['git','ls-tree','-r','-z','3305d4a','--','Tests'],cwd=ROOT).split(b'\0')
changed=[];checked=0
for entry in basefiles:
 if not entry:continue
 meta,name=entry.split(b'\t');name=name.decode();data=(ROOT/name).read_bytes();oid=hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest();checked+=1
 if oid!=meta.split()[2].decode():changed.append(name)
report=dict(baseline='3305d4a729e965247b33c95e5558eac576f1c6ef',baseline_report='Tests/UsbBulkOut/reports/regression-p1c-c0.json',current_tag=arg.tag,original_test_files_checked=checked,original_test_files_changed=changed,suites=rows,entry_summary=dict(baseline_pass=sum(r['baseline_exit']==0 for r in rows),baseline_fail=sum(r['baseline_exit']!=0 for r in rows),current_pass=sum(r['current_exit']==0 for r in rows),current_fail=sum(r['current_exit']!=0 for r in rows)),changes=[dict(suite=r['suite'],case=c) for r in rows for c in r['case_comparison'] if c['verdict']!='UNCHANGED'])
(OUT/'case_comparison.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(dict(entries=report['entry_summary'],cases=sum(r['current_cases'] for r in rows),changes=len(report['changes']),changed_original_tests=changed),ensure_ascii=False))
assert not changed and not report['changes']
assert all(r['baseline_exit']==r['current_exit'] for r in rows)
