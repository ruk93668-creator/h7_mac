"""Compare default preprocessed code against R1B; ignore only diagnostic file/line."""
from pathlib import Path
import subprocess,xml.etree.ElementTree as E,shlex,hashlib,json,re,time
R=Path(__file__).resolve().parents[2];out=R/'output/TinyUSBReplacement'/('r2-default-check-'+str(time.time_ns()));out.mkdir()
t=next(t for t in E.parse(R/'MDK-ARM/h7_mac.uvprojx').findall('Targets/Target') if t.findtext('TargetName')=='h7_mac_CM7');v=t.find('TargetOption/TargetArmAds/Cads/VariousControls')
a=['D:/32/ARM/ARMCC/Bin/armcc.exe','--cpu','Cortex-M7','--c99','--gnu','-E','-I',str(R/'CM7/Application/Tasks')]
for d in v.findtext('Define').split(','):a+=['-D',d]
for inc in v.findtext('IncludePath').split(';'):a+=['-I',str((R/'MDK-ARM'/inc).resolve())]
a+=shlex.split(v.findtext('MiscControls'));rows=[]
for f in ['CM7/Core/Src/main.c','CM7/Application/Tasks/usb_task.c']:
 old=out/('baseline_'+Path(f).name);old.write_bytes(subprocess.check_output(['git','show','afbcf60:'+f],cwd=R));results=[]
 for p in [old,R/f]:
  x=subprocess.run(a+[str(p)],cwd=R/'MDK-ARM',capture_output=True);assert x.returncode==0,x.stderr
  text=x.stdout.decode(errors='replace');text='\n'.join(l for l in text.splitlines() if not l.startswith('#'))
  text=re.sub(r'Platform_RtosAssert\("[^" ]+",\s*\d+\)', 'Platform_RtosAssert(DIAGNOSTIC_FILE,DIAGNOSTIC_LINE)',text)
  results.append(re.sub(r'\s+','',text))
 assert results[0]==results[1],f
 rows.append(dict(path=f,default_tokens_except_assert_location_unchanged=True,sha256=hashlib.sha256(results[1].encode()).hexdigest()))
(R/'Tests/TinyUSBReplacement/reports/R2_default_behavior.json').write_text(json.dumps(dict(rows=rows,boundary='Preprocessor equivalence except assertion file/line; no hardware test',fixture_history=['Initial baseline_usb_task probe missed source-local Tasks include','Raw comparison then differed only in Platform_RtosAssert file/line, examined before diagnostic-only normalization']),indent=2),encoding='utf-8');print(rows)
