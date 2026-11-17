from pathlib import Path
import subprocess,zipfile,io,datetime,json,hashlib,shutil,re
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'output/V2Phase2'/('build_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'));OUT.mkdir(parents=True)
SNAP=OUT/'workspace';SNAP.mkdir()
archive=subprocess.check_output(['git','archive','--format=zip','HEAD'],cwd=ROOT)
with zipfile.ZipFile(io.BytesIO(archive)) as z:z.extractall(SNAP)
# Copy exactly the existing external project settings, never edit originals.
protected={}
for name in ['h7_mac.uvprojx','h7_mac.uvoptx']:
 p=ROOT/'MDK-ARM'/name;protected[str(p)]=hashlib.sha256(p.read_bytes()).hexdigest();shutil.copy2(p,SNAP/'MDK-ARM'/name)
shutil.copytree(ROOT/'CM7/ApplicationV2/Resource',SNAP/'CM7/ApplicationV2/Resource',dirs_exist_ok=True)
report={'source_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT).decode().strip(),'snapshot':str(SNAP),'external_project_hashes':protected,'targets':[]}
for target in ['h7_mac_CM7','h7_mac_CM4']:
 log=OUT/(target+'.log');cmd=['D:/32/UV4/UV4.exe','-j0','-r',str(SNAP/'MDK-ARM/h7_mac.uvprojx'),'-t',target,'-o',str(log)]
 si=subprocess.STARTUPINFO();si.dwFlags|=subprocess.STARTF_USESHOWWINDOW
 p=subprocess.run(cmd,cwd=SNAP,startupinfo=si,creationflags=subprocess.CREATE_NO_WINDOW)
 text=log.read_bytes().decode('utf-8',errors='replace') if log.exists() else ''
 artifacts=[]
 for f in (SNAP/'MDK-ARM'/target).rglob('*'):
  if f.is_file() and f.suffix.lower() in ['.axf','.map','.htm','.hex']:
   artifacts.append({'path':str(f),'sha256':hashlib.sha256(f.read_bytes()).hexdigest(),'bytes':f.stat().st_size})
 row={'target':target,'exit':p.returncode,'zero_errors_warnings':bool(re.search(r'0 Error\(s\), 0 Warning\(s\)',text)),'program_size':re.findall(r'Program Size:.*',text),'artifacts':artifacts};report['targets'].append(row)
 (OUT/'results.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(row['target'],row['exit'],row['zero_errors_warnings'],row['program_size'],flush=True)
assert all(hashlib.sha256(Path(p).read_bytes()).hexdigest()==h for p,h in protected.items()),'external project changed during build; inspect concurrent edits'
print(OUT);raise SystemExit(any(t['exit'] or not t['zero_errors_warnings'] for t in report['targets']))
