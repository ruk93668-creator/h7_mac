from pathlib import Path
import subprocess,sys,json
p=Path('output/Ethernet/q10host');p.mkdir(parents=True, exist_ok=True)
cases={'e4':['run_e4_host.py'],'e42':['run_e4_host.py','--dhcp'],'e5':['run_e5_host.py'],'e6':['run_e6_host.py'],'e7':['run_e6_host.py','--dynamic'],'e7-qvga':['run_e6_host.py','--dynamic','--qvga']}
res={}
for name, args in cases.items():
 with (p/(name+'.log')).open('wb') as f:
  result=subprocess.run([sys.executable,'-B','Tests/Ethernet/'+args[0],*args[1:]],stdout=f,stderr=subprocess.STDOUT,timeout=180)
 res[name]=result.returncode
 print(name,result.returncode,flush=True)
(p/'host-exit.json').write_text(json.dumps(res,indent=2))
raise SystemExit(any(res.values()))
