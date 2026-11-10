import os,subprocess,re,json,hashlib
from pathlib import Path
root=Path(__file__).resolve().parents[2];out=Path(os.environ['TEMP'])/'h7_stageA_48afa27';project=root/'MDK-ARM/h7_mac.uvprojx';rows=[]
def digest():
 h=hashlib.sha256()
 for d in ['CM7','CM4','Common']:
  for p in sorted((root/d).rglob('*')):
   if p.suffix in ['.c','.h']:h.update(p.read_bytes())
 h.update(project.read_bytes());return h.hexdigest()
for target in ['h7_mac_CM7','h7_mac_CM4']:
 before=digest();log=out/(target+'_rebuild.log');cmd=['D:/32/UV4/UV4.exe','-j0','-r',str(project),'-t',target,'-o',str(log)]
 st=subprocess.STARTUPINFO();st.dwFlags|=subprocess.STARTF_USESHOWWINDOW;st.wShowWindow=0
 p=subprocess.run(cmd,cwd=root,startupinfo=st,creationflags=subprocess.CREATE_NO_WINDOW);text=log.read_text(errors='replace');m=list(re.finditer(r'(\d+) Error\(s\), (\d+) Warning\(s\)',text));m=m[-1] if m else None
 r=dict(target=target,exit=p.returncode,errors=int(m[1]) if m else None,warnings=int(m[2]) if m else None,source_unchanged=before==digest());rows.append(r);(out/'build_results.json').write_text(json.dumps(rows,indent=2));print(r,flush=True)
 if not m or int(m[1]) or int(m[2]):print(text[-6000:],flush=True)
