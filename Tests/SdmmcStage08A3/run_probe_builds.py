"""Compile controlled probe profiles using the same formal project, then restore it byte-for-byte."""
from pathlib import Path
import subprocess,json,re,hashlib,shutil
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
PROJECT=ROOT/'MDK-ARM/h7_mac.uvprojx';OUT=ROOT/'output/SdmmcStage08A3/firmware';OUT.mkdir(parents=True,exist_ok=True)
original=PROJECT.read_bytes();rows=[]
try:
 for name,detect in [('detect',1),('probe',0)]:
  text=original.decode('utf-8');m=re.search(r'<Target>\s*<TargetName>h7_mac_CM7</TargetName>.*?</Target>',text,re.S);target=m.group(0)
  c=re.search(r'<Cads>.*?</Cads>',target,re.S);cads=c.group(0)
  d=re.search(r'<Define>(.*?)</Define>',cads,re.S)
  value=d.group(1)+',APP_SDMMC_TEST_ENABLED=1,APP_SDMMC_DETECT_ONLY='+str(detect)
  cads=cads[:d.start(1)]+value+cads[d.end(1):];target=target[:c.start()]+cads+target[c.end():]
  PROJECT.write_bytes((text[:m.start()]+target+text[m.end():]).encode('utf-8'))
  log=HERE/('CM7_'+name+'_rebuild.log');cmd=['D:/32/UV4/UV4.exe','-j0','-r',str(PROJECT),'-t','h7_mac_CM7','-o',str(log)]
  startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
  print('START',name,flush=True);r=subprocess.run(cmd,cwd=ROOT,startupinfo=startup,creationflags=subprocess.CREATE_NO_WINDOW)
  content=log.read_text(errors='replace');matches=list(re.finditer(r'(\d+) Error\(s\), (\d+) Warning\(s\)',content));last=matches[-1] if matches else None
  row=dict(profile=name,command=subprocess.list2cmdline(cmd),exit_code=r.returncode,errors=int(last[1]) if last else None,warnings=int(last[2]) if last else None)
  row['passed']=r.returncode==0 and row['errors']==0 and row['warnings']==0
  if row['passed']:
   folder=OUT/name;folder.mkdir(exist_ok=True)
   for suffix in ['.axf','.hex','.map']:
    src=ROOT/('MDK-ARM/h7_mac_CM7/h7_mac_CM7'+suffix)
    if src.exists():shutil.copy2(src,folder/src.name)
   mapping=(folder/'h7_mac_CM7.map').read_text(errors='replace');match=re.search(r'g_sdmmc_test\s+0x([0-9a-fA-F]+)\s+Data\s+(\d+)',mapping)
   row['watch_address']='0x'+match[1] if match else None;row['watch_bytes']=int(match[2]) if match else None
   row['passed']=row['passed'] and match is not None
   row['axf_sha256']=hashlib.sha256((folder/'h7_mac_CM7.axf').read_bytes()).hexdigest()
  rows.append(row);(HERE/'probe_build_results.json').write_text(json.dumps(rows,indent=2),encoding='utf-8');print(row,flush=True)
  if not row['passed']:raise RuntimeError(content[-7000:])
finally:
 PROJECT.write_bytes(original)
 (HERE/'probe_project_restoration.json').write_text(json.dumps(dict(project_restored=PROJECT.read_bytes()==original,project_sha256=hashlib.sha256(original).hexdigest()),indent=2),encoding='utf-8')
