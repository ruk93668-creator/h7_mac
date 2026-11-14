"""Rebuild formal CM7/CM4 and the isolated watch target, preserving user settings."""
from pathlib import Path
import subprocess,json,re,hashlib,shutil,argparse
from prepare_watch_project import prepare,ROOT,HERE,OUT
OUT.mkdir(parents=True,exist_ok=True)
formal=ROOT/'MDK-ARM/h7_mac.uvprojx';before=hashlib.sha256(formal.read_bytes()).hexdigest();watch=prepare()
parser=argparse.ArgumentParser();parser.add_argument('--watch-only',action='store_true');args=parser.parse_args()
rows=[]
if args.watch_only:
 saved=json.loads((HERE/'watch_build_results.json').read_text(encoding='utf-8'));assert saved['formal_project_sha256']==before
 rows=[row for row in saved['targets'] if row['variant']!='watch'];assert len(rows)==2 and all(row['pass'] for row in rows)
for variant,project,target,folder in [('formal_cm7',formal,'h7_mac_CM7',ROOT/'MDK-ARM/h7_mac_CM7'),('formal_cm4',formal,'h7_mac_CM4',ROOT/'MDK-ARM/h7_mac_CM4'),('watch',watch,'h7_mac_CM7_R2_Watch',OUT/'firmware')]:
 if args.watch_only and variant!='watch':continue
 backup=OUT/'previous_output'/variant
 if folder.exists() and not backup.exists():shutil.copytree(folder,backup)
 log=OUT/(variant+'_rebuild.log')
 command=['D:/32/UV4/UV4.exe','-j0','-r',str(project),'-t',target,'-o',str(log)]
 startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
 p=subprocess.run(command,cwd=ROOT,startupinfo=startup,creationflags=subprocess.CREATE_NO_WINDOW)
 raw=log.read_bytes() if log.exists() else b'';text=raw.decode('utf-8',errors='replace');m=re.search(r'(\d+) Error\(s\), (\d+) Warning\(s\)',text)
 (HERE/(variant+'_rebuild.log')).write_bytes(raw)
 row={'variant':variant,'target':target,'command':command,'exit':p.returncode,'errors':int(m[1]) if m else None,'warnings':int(m[2]) if m else None,'pass':p.returncode==0 and bool(m) and m.groups()==('0','0'),'log_sha256':hashlib.sha256(raw).hexdigest()}
 axf=folder/(target+'.axf');mp=folder/(target+'.map')
 row['axf']=str(axf);row['axf_sha256']=hashlib.sha256(axf.read_bytes()).hexdigest() if axf.exists() else None
 if mp.exists() and row['pass']:
  data=mp.read_text(errors='replace');row['mailbox_present']='g_r2_hw' in data
  totals=re.search(r'^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+Grand Totals',data,re.M)
  row['totals']=list(map(int,totals.groups())) if totals else None
  symbol=re.search(r'\bg_r2_hw\s+(0x[\da-fA-F]+)\s+Data\s+(\d+)',data)
  if symbol:row['mailbox']={'address':symbol[1],'bytes':int(symbol[2])}
  row['isolation_pass']=(row['mailbox_present'] if variant=='watch' else not row['mailbox_present'])
  if variant=='watch':
   row['control_hook_linked']='control_task.o(i.controltask_run) refers to hardwarecontrol.o(i.r2_hardwarecontrolprocess)' in data.lower()
 rows.append(row)
 report={'project_unchanged':hashlib.sha256(formal.read_bytes()).hexdigest()==before,'formal_project_sha256':before,'generated_watch_project':str(watch),'targets':rows,'hardware_verified':False}
 (HERE/'watch_build_results.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
 print(variant,row,flush=True)
 if not row['pass']:print(text[-4000:],flush=True);break
assert hashlib.sha256(formal.read_bytes()).hexdigest()==before
raise SystemExit(not(len(rows)==3 and all(r['pass'] and r.get('isolation_pass') for r in rows) and rows[-1].get('control_hook_linked')))
