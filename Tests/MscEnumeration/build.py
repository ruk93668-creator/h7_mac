from pathlib import Path
import subprocess, json, re, shutil
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'output/MSC1'; OUT.mkdir(parents=True,exist_ok=True)
project=ROOT/'MDK-ARM/h7_mac.uvprojx'
temporary=ROOT/'MDK-ARM/msc1_enumeration.uvprojx'
assert not temporary.exists()
raw=project.read_bytes(); data=raw.decode('utf-8')
at=data.index('<TargetName>h7_mac_CM7</TargetName>')
end=data.index('</Define>',data.index('<Define>',at))
data=data[:end]+',USB_MODE_BOOT_MSC=1'+data[end:]
temporary.write_bytes(data.encode('utf-8'))
rows=[]
try:
 for mode, path in [('msc',temporary),('default_uvc',project)]:
  log=OUT/(mode+'_cm7.log')
  startup=subprocess.STARTUPINFO(); startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW; startup.wShowWindow=0
  cmd=['D:/32/UV4/UV4.exe','-j0','-r',str(path),'-t','h7_mac_CM7','-o',str(log)]
  r=subprocess.run(cmd,cwd=ROOT,startupinfo=startup,creationflags=subprocess.CREATE_NO_WINDOW)
  text=log.read_text(encoding='utf-8',errors='replace') if log.exists() else ''
  m=re.search(r'(\d+) Error\(s\), (\d+) Warning\(s\)',text)
  passed=r.returncode==0 and m is not None and m.groups()==('0','0')
  rows.append(dict(mode=mode,exit=r.returncode,summary=m.group(0) if m else None,passed=passed))
  for suffix in ['hex','axf','map']:
   artifact=ROOT/'MDK-ARM/h7_mac_CM7'/('h7_mac_CM7.'+suffix)
   if artifact.exists(): shutil.copy2(artifact,OUT/(mode+'.'+suffix))
  print(rows[-1],flush=True)
  if not passed: print(text[-5000:],flush=True)
finally:
 temporary.unlink()
 assert project.read_bytes()==raw
 (OUT/'build_results.json').write_text(json.dumps(rows,indent=2))
raise SystemExit(not all(x['passed'] for x in rows))
