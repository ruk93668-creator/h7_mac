"""Run new joint contracts and isolated exit safety; preserve every nonzero stage result."""
from pathlib import Path
import subprocess,sys,json,time
H=Path(__file__).resolve().parent;ROOT=H.parents[1]
OUT=ROOT/'output/ConsumerJoint'/('acceptance_'+str(time.time_ns()));OUT.mkdir(parents=True)
rows=[]
for script in ['run_tests.py','check_contracts.py','run_exit.py','audit_gates.py','audit_resources.py']:
 p=subprocess.run([sys.executable,str(H/script)],cwd=ROOT,capture_output=True)
 (OUT/(script+'.log')).write_bytes(p.stdout+p.stderr)
 rows.append({'runner':script,'exit':p.returncode,'log':str(OUT/(script+'.log'))});print(rows[-1],flush=True)
(OUT/'execution.json').write_text(json.dumps(rows,indent=2))
raise SystemExit(any(r['exit']!=0 for r in rows))
