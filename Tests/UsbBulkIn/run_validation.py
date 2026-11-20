"""Disposable build/regression snapshots; never edit original tests or project/RTE files."""
from pathlib import Path
import argparse, subprocess, shutil, hashlib, json, os, sys, time, re
ROOT=Path(__file__).resolve().parents[2];REPORT=ROOT/'Tests/UsbBulkIn/reports'
SUITES=['UsbCoreDecoupling/run_tests.py','UvcLifecycle/run_tests.py','UvcStreaming/run_tests.py','UvcPayload/run_tests.py','UvcEp1Lifecycle/run_tests.py','UvcProbeCommit/run_tests.py','UvcProbeCommit/run_cancellation_tests.py','Ep0IrqRace/run_race.py','UsbUvcDescriptors/run_tests.py','UvcUsbCleanup/run_tests.py','MscDeferredStatus/run_tests.py','MscProtocol/run_tests.py','MscBlockAdapter/run_tests.py','MscSdIntegration/run_tests.py','MscEnumeration/run_tests.py','MscEnumeration/run_integration.py','MscSenseCompat/run_tests.py']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--build',action='store_true');ap.add_argument('--tag',required=True);args=ap.parse_args()
 assert re.fullmatch(r'[A-Za-z0-9-]+',args.tag)
 label=('build-' if args.build else 'regression-')+args.tag
 work=ROOT/'output/MSC24/udlm-p1b'/label
 assert not work.exists(),str(work)
 work.mkdir(parents=True);REPORT.mkdir(parents=True,exist_ok=True)
 manifest={}
 for n in subprocess.check_output(['git','ls-files','-z'],cwd=ROOT).decode().split('\0'):
  if not n or n.split('/')[0] not in ('CM7','CM4','Common','Board','Drivers','Middlewares','MDK-ARM','Tests'):continue
  src=ROOT/n
  if not src.is_file():continue
  dst=work/n;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst);manifest[n]=digest(src)
 env=dict(os.environ,PYTHONPATH=str(ROOT/'output/UDLM-P1/python_deps'),PYTHONIOENCODING='utf-8',PYTHONDONTWRITEBYTECODE='1')
 before=json.loads((ROOT/'Tests/EndpointLifecycle/reports/baseline-regression-p1b-before.json').read_text(encoding='utf-8'))
 byname={r['suite']:r for r in before['rows']}
 rows=[]
 for suite in (['MscReconnect/build.py'] if args.build else SUITES):
  start=time.monotonic();p=subprocess.run([sys.executable,str(work/'Tests'/suite)],cwd=work,env=env,capture_output=True)
  raw=p.stdout+p.stderr;log=REPORT/(label+'-'+suite.replace('/','_').replace('.py','.log'));log.write_bytes(raw)
  emitted=[]
  for f in (work/'Tests'/suite.split('/')[0]).glob('*.json'):
   n=f.relative_to(work).as_posix()
   if n not in manifest or digest(f)!=manifest[n]:
    try:emitted.append(dict(path=n,sha256=digest(f),content=json.loads(f.read_text(encoding='utf-8'))))
    except (ValueError,UnicodeError):pass
  text=raw.decode('utf-8',errors='replace')
  row=dict(suite=suite,exit=p.returncode,elapsed_s=round(time.monotonic()-start,3),log=str(log.relative_to(ROOT)),log_sha256=digest(log),produced_reports=emitted,tail=text[-2000:])
  if not args.build:
   row['baseline_exit']=byname[suite]['exit'];row['baseline_log']=byname[suite]['log']
   row['new_link_dependency_missing']='Undefined symbol BSP_USB_BulkInHasReferences' in text
   row['classification']='NOT_RUN: legacy BSP double missing new query' if row['new_link_dependency_missing'] else 'PASS' if p.returncode==0 else 'FAIL'
   # Retain per-case report values verbatim; never edit assertions or replace test doubles here.
   row['case_report_exact_match']= [x['content'] for x in emitted]==[x['content'] for x in byname[suite]['produced_reports']]
  rows.append(row)
  report=dict(baseline='5a27af621c219ead2bc680858efd4aeb17e110a4',snapshot=str(work),mode=label,unchanged_original_test_inputs=True,rows=rows,original_changed=[n for n,h in manifest.items() if digest(ROOT/n)!=h])
  (REPORT/(label+'.json')).write_bytes(json.dumps(report,ensure_ascii=False,indent=2).encode('utf-8'))
  print(label,suite,'EXIT',p.returncode,text[-180:].replace('\n',' '),flush=True)
 print('FINISHED',label,'entries',len(rows),'failed exits',sum(r['exit']!=0 for r in rows),flush=True)
 assert not report['original_changed']
if __name__=='__main__':main()
