"""Run unchanged historical suites; archive fresh results here and retain their baseline reports."""
from pathlib import Path
import subprocess,json,hashlib,sys
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/OledMonitor/regressions';OUT.mkdir(parents=True,exist_ok=True)
suites=[('IpcV2','run_tests.py','results.json'),('IpcRuntime','run_tests.py','results.json'),
 ('IpcRuntime','run_task_tests.py','task_results.json'),('IpcStatus','run_tests.py','results.json'),
 ('IpcStatus','run_kernel_tests.py','task_results.json'),('OledAsyncAcceptance','run_tests.py','results.json')]
rows=[]
for folder,script,report in suites:
 directory=ROOT/'Tests'/folder
 # Keep historical tracked reports unchanged; store this run's evidence in the new acceptance directory.
 tracked=subprocess.check_output(['git','-c','core.quotepath=false','ls-files','--',str(directory.relative_to(ROOT))],cwd=ROOT).decode('utf-8').splitlines()
 saved={name:(ROOT/name).read_bytes() for name in tracked}
 label=folder+'_'+Path(script).stem
 cmd=[sys.executable,str(directory/script)]
 try:
  r=subprocess.run(cmd,cwd=ROOT,capture_output=True)
  log=r.stdout+r.stderr
  (HERE/(label+'.log')).write_bytes(log)
  data=json.loads((directory/report).read_text(encoding='utf-8')) if (directory/report).exists() else {}
  (HERE/(label+'.json')).write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
  row=dict(suite=label,command=subprocess.list2cmdline(cmd),exit_code=r.returncode,summary=data.get('summary'),source_unchanged=all((ROOT/n).read_bytes()==b for n,b in saved.items() if n.endswith(('.c','.h','.py','.sct'))))
  rows.append(row);print(row,flush=True)
 finally:
  for name,content in saved.items():
   if (ROOT/name).read_bytes()!=content:(ROOT/name).write_bytes(content)
 (HERE/'regression_results.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
raise SystemExit(any(r['exit_code'] or not r['source_unchanged'] for r in rows))
