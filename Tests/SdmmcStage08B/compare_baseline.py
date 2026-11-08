"""Reproduce failed historical suites against a source-only archive of d0305de, without touching the checkout."""
from pathlib import Path
import subprocess,tarfile,os,json,sys
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
DEST=ROOT/'output/SdmmcStage08B/baseline_d0305de';DEST.mkdir(parents=True,exist_ok=True)
p=subprocess.Popen(['git','archive','d0305de','CM7','CM4','Common','Board','Drivers','Middlewares','Tests'],cwd=ROOT,stdout=subprocess.PIPE)
with tarfile.open(fileobj=p.stdout,mode='r|') as tar:
 for m in tar:
  if not m.isfile() or Path(m.name).suffix.lower() not in ('.c','.h','.py','.sct','.json','.ini'):continue
  out=(DEST/m.name).resolve()
  if not out.is_relative_to(DEST.resolve()):raise RuntimeError('archive path escape')
  out.parent.mkdir(parents=True,exist_ok=True)
  with tar.extractfile(m) as source:out.write_bytes(source.read())
if p.wait():raise RuntimeError('git archive failed')
print('BASELINE archive ready',flush=True)
# Use local, untracked Python dependencies; no source from the edited checkout is substituted.
env=os.environ.copy();env['PYTHONPATH']=str(ROOT/'Tests/OledAsyncAcceptance/python_deps')+os.pathsep+env.get('PYTHONPATH','')
rows=[]
def summary(data):
 if isinstance(data,list):return dict(total=len(data),passed=sum(bool(x.get('pass')) for x in data),failed=sum(not bool(x.get('pass')) for x in data))
 return data.get('summary')
for row in json.loads((HERE/'regression_results.json').read_text()):
 if row['exit_code']==0:continue
 cmd=row['command'];# Recover exact structured arguments from the suite mapping, not shell-splitting Windows commands.
 folder=row.get('folder',row['suite'].split('_run_',1)[0]);script=row.get('script','run_'+row['suite'].split('_run_',1)[1]+'.py');runner=script[4:-3]
 name=row['suite']
 candidates=[p for p in (DEST/'Tests'/folder).glob('*results.json')]
 if runner=='cancellation_tests':report='cancellation_results.json';extra=[]
 elif folder=='CameraMjpegStage03B':
  stem={'media':'media_test','native':'native_test','usb':'usb_test'}[runner];report=stem+'_results.json';extra=[stem,'--count',str({'media':16,'native':15,'usb':13}[runner])]
 elif folder=='NativeRuntimeStage02':
  stem={'native':'phase02d','usb':'phase02b','bsp':'phase02a_bsp'}[runner];report=stem+'_results.json';extra=[stem,'--count',str({'native':33,'usb':11,'bsp':7}[runner])]
 else:report='results.json';extra=[]
 if 'args' in row:report=row['report'];extra=row['args']
 scriptpath=DEST/'Tests'/folder/script;resultpath=scriptpath.parent/report;resultpath.unlink(missing_ok=True)
 call=[sys.executable,str(scriptpath)]+extra
 r=subprocess.run(call,cwd=DEST,env=env,capture_output=True)
 (HERE/(name+'_baseline.log')).write_bytes(r.stdout+r.stderr)
 data=json.loads(resultpath.read_text(encoding='utf-8')) if resultpath.exists() else {}
 current=json.loads((HERE/(name+'.json')).read_text(encoding='utf-8'))
 def failures(d):
  cases=d if isinstance(d,list) else d.get('cases',[])
  return [str(x.get('id',x.get('case',x.get('name',x.get('scenario',i))))) for i,x in enumerate(cases) if x.get('result')=='FAIL' or x.get('pass') is False or x.get('passed') is False]
 out=dict(suite=name,baseline='d0305de',command=subprocess.list2cmdline(call),baseline_exit=r.returncode,current_exit=row['exit_code'],baseline_summary=summary(data),current_summary=row['summary'],same_summary=summary(data)==row['summary'],baseline_failures=failures(data),current_failures=failures(current))
 out['same_failures']=out['baseline_failures']==out['current_failures'];rows.append(out)
 print(out,flush=True)
 (HERE/'baseline_comparison.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
raise SystemExit(any(not x['same_summary'] or not x['same_failures'] for x in rows))
