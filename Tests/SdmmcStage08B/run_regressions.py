"""Run unchanged historical suites; archive fresh results here and retain their baseline reports."""
from pathlib import Path
import subprocess,json,hashlib,sys
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/SdmmcStage08B/regressions';OUT.mkdir(parents=True,exist_ok=True)
suites=[('IpcV2','run_tests.py','results.json'),('IpcRuntime','run_tests.py','results.json'),
 ('IpcRuntime','run_task_tests.py','task_results.json'),('IpcStatus','run_tests.py','results.json'),
 ('IpcStatus','run_kernel_tests.py','task_results.json'),('OledAsyncAcceptance','run_tests.py','results.json'),('OledMonitor','run_tests.py','results.json'),
 ('UsbCoreDecoupling','run_tests.py','results.json'),('Ep0IrqRace','run_race.py','results.json'),
 ('UvcPayload','run_tests.py','results.json'),('UvcProbeCommit','run_tests.py','results.json'),
 ('UvcProbeCommit','run_cancellation_tests.py','cancellation_results.json'),
 ('UvcEp1Lifecycle','run_tests.py','results.json'),('UvcStreaming','run_tests.py','results.json'),
 ('CameraMjpegStage03B','run_media.py','media_test_results.json',['media_test','--count','16']),
 ('CameraMjpegStage03B','run_native.py','native_test_results.json',['native_test','--count','15']),
 ('CameraMjpegStage03B','run_usb.py','usb_test_results.json',['usb_test','--count','13']),
 ('CameraStreamState','run_tests.py','results.json'),
 ('CameraSingleFrame/Camera','run_tests.py','results.json'),
 ('CameraFormats/Camera','run_tests.py','results.json'),
 ('NativeRuntimeStage02','run_native.py','phase02d_results.json',['phase02d','--count','33']),
 ('NativeRuntimeStage02','run_usb.py','phase02b_results.json',['phase02b','--count','11']),
 ('NativeRuntimeStage02','run_bsp.py','phase02a_bsp_results.json',['phase02a_bsp','--count','7']),('DcmiReplaceTarget','run_tests.py','results.json')]
def write_cases(path, data):
 # Keep every observation, but place repeated cases on one line instead of duplicating thousands of pretty-print lines.
 if isinstance(data,dict) and isinstance(data.get('cases'),list):
  metadata={k:v for k,v in data.items() if k!='cases'}
  text=json.dumps(metadata,ensure_ascii=False,indent=2)[:-1].rstrip()
  text+=',\n  "cases": [\n'+',\n'.join('    '+json.dumps(c,ensure_ascii=False,separators=(',',':')) for c in data['cases'])+'\n  ]\n}\n'
 else:text=json.dumps(data,ensure_ascii=False,indent=2)+'\n'
 path.write_text(text,encoding='utf-8')

rows=json.loads((HERE/'regression_results.json').read_text()) if '--resume' in sys.argv else []
completed=len(rows)
for index,item in enumerate(suites):
 if index<completed:continue
 folder,script,report=item[:3];extra=item[3] if len(item)>3 else []
 directory=ROOT/'Tests'/folder
 # Keep historical tracked reports unchanged; store this run's evidence in the new acceptance directory.
 tracked=subprocess.check_output(['git','-c','core.quotepath=false','ls-files','--',str(directory.relative_to(ROOT))],cwd=ROOT).decode('utf-8').splitlines()
 saved={name:(ROOT/name).read_bytes() for name in tracked}
 label=folder.replace('/','_')+'_'+Path(script).stem
 cmd=[sys.executable,str(directory/script)]+extra
 try:
  (directory/report).unlink(missing_ok=True)
  r=subprocess.run(cmd,cwd=ROOT,capture_output=True)
  log=r.stdout+r.stderr
  (HERE/(label+'.log')).write_bytes(log)
  data=json.loads((directory/report).read_text(encoding='utf-8')) if (directory/report).exists() else {}
  write_cases(HERE/(label+'.json'),data)
  row=dict(suite=label,folder=folder,script=script,report=report,args=extra,command=subprocess.list2cmdline(cmd),exit_code=r.returncode,summary=(data.get('summary') if isinstance(data,dict) else dict(total=len(data),passed=sum(bool(x.get('pass')) for x in data),failed=sum(not bool(x.get('pass')) for x in data))),source_unchanged=all((ROOT/n).read_bytes()==b for n,b in saved.items() if n.endswith(('.c','.h','.py','.sct'))))
  rows.append(row);print(row,flush=True)
 finally:
  for name,content in saved.items():
   if not (ROOT/name).exists() or (ROOT/name).read_bytes()!=content:(ROOT/name).write_bytes(content)
 (HERE/'regression_results.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
raise SystemExit(any(r['exit_code'] or not r['source_unchanged'] for r in rows))
