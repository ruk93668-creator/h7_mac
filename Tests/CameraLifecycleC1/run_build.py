"""Full rebuild of unchanged formal project; preserve project settings and prior output."""
from pathlib import Path
import subprocess, json, re, shutil, hashlib
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/CameraLifecycleC1/build';OUT.mkdir(parents=True,exist_ok=True)
project=ROOT/'MDK-ARM/h7_mac.uvprojx'
before=hashlib.sha256(project.read_bytes()).hexdigest()
rows=[]
for target in ['h7_mac_CM7','h7_mac_CM4']:
 folder=ROOT/'MDK-ARM'/target
 backup=OUT/'previous_output'/target
 if folder.exists() and not backup.exists():shutil.copytree(folder,backup)
 log=OUT/(target+'_rebuild.log')
 cmd=['D:/32/UV4/UV4.exe','-j0','-r',str(project),'-t',target,'-o',str(log)]
 startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
 p=subprocess.run(cmd,cwd=ROOT,startupinfo=startup,creationflags=subprocess.CREATE_NO_WINDOW)
 raw=log.read_bytes() if log.exists() else b''
 text=raw.decode('utf-8',errors='replace')
 m=re.search(r'(\d+) Error\(s\), (\d+) Warning\(s\)',text)
 row={'target':target,'command':cmd,'exit':p.returncode,'errors':int(m[1]) if m else None,'warnings':int(m[2]) if m else None}
 row['pass']=bool(m) and m.groups()==('0','0') and p.returncode==0
 row['artifacts']={suffix:str(folder/(target+suffix)) for suffix in ['.axf','.hex','.map']}
 row['artifacts_exist']={suffix:Path(path).exists() for suffix,path in row['artifacts'].items()}
 row['log_sha256']=hashlib.sha256(raw).hexdigest()
 (HERE/(target+'_rebuild.log')).write_bytes(raw)
 rows.append(row)
 (HERE/'build_results.json').write_text(json.dumps({'targets':rows,'project_unchanged':hashlib.sha256(project.read_bytes()).hexdigest()==before},indent=2),encoding='utf-8')
 print(target,row,flush=True)
 if not row['pass']:print(text[-3000:],flush=True)
assert hashlib.sha256(project.read_bytes()).hexdigest()==before
raise SystemExit(not all(r['pass'] for r in rows))
