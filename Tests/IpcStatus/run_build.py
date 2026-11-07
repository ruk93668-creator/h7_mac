"""Rebuild the two formal targets without regenerating or rewriting Keil projects."""
from pathlib import Path
import subprocess,json,re,hashlib,sys
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
PROJECT=ROOT/'MDK-ARM/h7_mac.uvprojx'
def digest():
 h=hashlib.sha256()
 for directory in ['CM7','CM4','Common']:
  for p in sorted((ROOT/directory).rglob('*')):
   if p.is_file() and p.suffix in ['.c','.h']:
    h.update(str(p.relative_to(ROOT)).encode());h.update(p.read_bytes())
 return h.hexdigest()
incremental="--incremental" in sys.argv
rows=[]
for target in ['h7_mac_CM7','h7_mac_CM4']:
 log=HERE/(target+('_incremental.log' if incremental else '_rebuild.log'));before=digest()
 project_before=hashlib.sha256(PROJECT.read_bytes()).hexdigest()
 cmd=['D:/32/UV4/UV4.exe','-j0','-b' if incremental else '-r',str(PROJECT),'-t',target,'-o',str(log)]
 print('START',target,flush=True)
 startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
 result=subprocess.run(cmd,cwd=ROOT,startupinfo=startup,creationflags=subprocess.CREATE_NO_WINDOW)
 text=log.read_text(errors='replace') if log.exists() else ''
 matches=list(re.finditer(r'(\d+) Error\(s\), (\d+) Warning\(s\)',text))
 last=matches[-1] if matches else None
 row=dict(target=target,command=subprocess.list2cmdline(cmd),exit_code=result.returncode,errors=int(last[1]) if last else None,warnings=int(last[2]) if last else None,source_fingerprint_before=before,source_fingerprint_after=digest(),project_sha_before=project_before,project_sha_after=hashlib.sha256(PROJECT.read_bytes()).hexdigest())
 row['passed']=row['exit_code']==0 and row['errors']==0 and row['warnings']==0 and row['source_fingerprint_before']==row['source_fingerprint_after']
 rows.append(row);(HERE/('incremental_build_results.json' if incremental else 'build_results.json')).write_text(json.dumps(rows,indent=2),encoding='utf-8')
 print(row,flush=True)
 if not row['passed']:print(text[-5000:],flush=True)
raise SystemExit(any(not r['passed'] for r in rows))
