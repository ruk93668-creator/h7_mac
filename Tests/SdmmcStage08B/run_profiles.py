"""Rebuild existing formal targets with temporary test defines; restore user project byte-for-byte."""
from pathlib import Path
import subprocess,sys,json,re,hashlib,shutil
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
PROJECT=ROOT/'MDK-ARM/h7_mac.uvprojx';OUT=ROOT/'output/SdmmcStage08B/firmware';OUT.mkdir(parents=True,exist_ok=True)
original=PROJECT.read_bytes();rows=[]
profiles=[('normal',0,0,0),('read_only',1,0,0),('write_compile_only',1,1,0),('read_stress',1,0,1)]
try:
 for label,enabled,write,stress in profiles:
  text=original.decode('utf-8-sig');m=re.search(r'<Target>\s*<TargetName>h7_mac_CM7</TargetName>.*?</Target>',text,re.S);target=m.group(0);c=re.search(r'<Cads>.*?</Cads>',target,re.S);cads=c.group(0);d=re.search(r'<Define>(.*?)</Define>',cads,re.S)
  defines=[x for x in d[1].split(',') if not x.strip().startswith(('APP_SDMMC_TEST_ENABLED','APP_SDMMC_DETECT_ONLY','APP_SD_BLOCK_TEST_ENABLED','APP_SD_BLOCK_WRITE_TEST_ENABLED','APP_SD_BLOCK_STRESS_TEST_ENABLED','APP_SD_BLOCK_D_GROUP_ENABLED','SD_BLOCK_STRESS_DURATION_MS','SD_BLOCK_STRESS_MAX_ROUNDS','SD_TEST_'))]
  defines+=['APP_SDMMC_TEST_ENABLED=0','APP_SD_BLOCK_TEST_ENABLED='+str(enabled),'APP_SD_BLOCK_WRITE_TEST_ENABLED='+str(write),'APP_SD_BLOCK_STRESS_TEST_ENABLED='+str(stress),'APP_SD_BLOCK_D_GROUP_ENABLED=0']
  if write:defines+=['SD_TEST_CID0=1U','SD_TEST_CID1=2U','SD_TEST_CID2=3U','SD_TEST_CID3=4U','SD_TEST_CAPACITY_BYTES=34359738368ULL','SD_TEST_WRITE_FIRST_LBA=100U','SD_TEST_WRITE_BLOCKS=8U']
  cads=cads[:d.start(1)]+','.join(defines)+cads[d.end(1):];target=target[:c.start()]+cads+target[c.end():];PROJECT.write_bytes((text[:m.start()]+target+text[m.end():]).encode('utf-8'))
  log=HERE/(label+'_rebuild.log');cmd=['D:/32/UV4/UV4.exe','-j0','-r',str(PROJECT),'-t','h7_mac_CM7','-o',str(log)]
  startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
  print('START',label,flush=True);p=subprocess.run(cmd,cwd=ROOT,startupinfo=startup,creationflags=subprocess.CREATE_NO_WINDOW)
  content=log.read_text(errors='replace');matches=list(re.finditer(r'(\d+) Error\(s\), (\d+) Warning\(s\)',content));last=matches[-1] if matches else None
  row=dict(profile=label,command=subprocess.list2cmdline(cmd),exit_code=p.returncode,errors=int(last[1]) if last else None,warnings=int(last[2]) if last else None)
  row['passed']=p.returncode==0 and row['errors']==0 and row['warnings']==0
  if row['passed']:
   folder=OUT/label;folder.mkdir(exist_ok=True)
   for suffix in ['.axf','.map']:
    src=ROOT/('MDK-ARM/h7_mac_CM7/h7_mac_CM7'+suffix)
    if src.exists():shutil.copy2(src,folder/src.name)
   row['axf_sha256']=hashlib.sha256((folder/'h7_mac_CM7.axf').read_bytes()).hexdigest()
   row['program_size']=next((x for x in content.splitlines() if 'Program Size:' in x),'')
  rows.append(row);(HERE/'profile_build_results.json').write_text(json.dumps(rows,indent=2),encoding='utf-8');print(row,flush=True)
  if not row['passed']:print(content[-8000:]);break
finally:
 PROJECT.write_bytes(original)
 (HERE/'project_restoration.json').write_text(json.dumps(dict(restored=PROJECT.read_bytes()==original,sha256=hashlib.sha256(original).hexdigest()),indent=2))
raise SystemExit(any(not x['passed'] for x in rows) or len(rows)!=len(profiles))
