from pathlib import Path
import subprocess,tarfile,io,time,json,re,hashlib
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/B2B6'/('formal_'+str(time.time_ns()));OUT.mkdir(parents=True)
g=['git','-c','safe.directory='+ROOT.as_posix()]
b=subprocess.check_output(g+['archive','HEAD'],cwd=ROOT)
with tarfile.open(fileobj=io.BytesIO(b)) as t:t.extractall(OUT,filter='data')
source_sha=subprocess.check_output(g+['rev-parse','HEAD'],cwd=ROOT).decode().strip()
rows=[]
for target in ['h7_mac_CM7','h7_mac_CM4']:
 log=OUT/(target+'.log');project=OUT/'MDK-ARM/h7_mac.uvprojx'
 startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
 result=subprocess.run(['D:/32/UV4/UV4.exe','-j0','-r',str(project),'-t',target,'-o',str(log)],startupinfo=startup,creationflags=subprocess.CREATE_NO_WINDOW)
 s=log.read_bytes().decode('utf-8',errors='replace');m=re.search(r'(\d+) Error\(s\), (\d+) Warning\(s\)',s)
 row={'target':target,'exit':result.returncode,'errors':int(m[1]) if m else None,'warnings':int(m[2]) if m else None,'artifacts':{}}
 for ext in ['axf','map','htm','hex']:
  f=OUT/'MDK-ARM'/target/(target+'.'+ext)
  if f.exists():row['artifacts'][ext]={'path':str(f),'sha256':hashlib.sha256(f.read_bytes()).hexdigest()}
 dest=HERE/'Evidence/build';dest.mkdir(parents=True,exist_ok=True);(dest/(target+'.log')).write_bytes(log.read_bytes())
 rows.append(row);(HERE/'build.json').write_text(json.dumps({'source_sha':source_sha,'snapshot':str(OUT),'targets':rows},indent=2));print(row,flush=True)
