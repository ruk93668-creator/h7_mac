from pathlib import Path
import json
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'output/MSC1'
files=['UvcStreaming/results.json','UvcPayload/results.json','UvcEp1Lifecycle/results.json','UvcProbeCommit/results.json','UvcProbeCommit/cancellation_results.json','UsbCoreDecoupling/results.json','Ep0IrqRace/results.json','UvcLifecycle/results.json','UvcUsbCleanup/results.json']
rows=[]
def view(d):
 if isinstance(d,list):return d
 return {'summary':d.get('summary'),'cases':d.get('cases')}
for name in files:
 a=json.loads((OUT/'regression-baseline/Tests'/name).read_text(encoding='utf-8'))
 b=json.loads((OUT/'regression-current/Tests'/name).read_text(encoding='utf-8'))
 va,vb=view(a),view(b)
 same=va==vb
 rows.append(dict(file=name,identical_cases_and_summary=same,baseline_summary=a.get('summary') if isinstance(a,dict) else None,current_summary=b.get('summary') if isinstance(b,dict) else None))
 print(name,same)
report={'baseline':'0d81ba137bd38dded3c8dcda56abae26d0cd3fd9','comparison':rows,'descriptor_suite':'main descriptor unit tests do not execute: missing camera_mjpeg_fifo.h include path in historical runner; string subtests 8/8 on both snapshots','warning':'baseline equivalence is not a green UVC regression result'}
(ROOT/'Tests/MscEnumeration/regression_comparison.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
assert all(row['identical_cases_and_summary'] for row in rows)
