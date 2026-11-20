"""Compile-only compatibility gate; does not build or claim production firmware."""
from pathlib import Path
import re
import subprocess,json,xml.etree.ElementTree as ET,hashlib,time
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
out=ROOT/'output/TinyUSBReplacement'/('compile-'+str(time.time_ns()));out.mkdir(parents=True)
t=next(t for t in ET.parse(ROOT/'MDK-ARM/h7_mac.uvprojx').findall('./Targets/Target') if t.findtext('TargetName')=='h7_mac_CM7')
base=['D:/32/ARM/ARMCC/Bin/armcc.exe','--cpu','Cortex-M7','--c99','--gnu','--split_sections','-O0','-DSTM32H747xx','-DCORE_CM7','-DUSE_HAL_DRIVER']
for inc in ['CM7/TinyUSB','ThirdParty/tinyusb/src']:base+=['-I',str(ROOT/inc)]
for inc in t.findtext('TargetOption/TargetArmAds/Cads/VariousControls/IncludePath').split(';'):base+=['-I',str((ROOT/'MDK-ARM'/inc).resolve())]
rows=[]
prior=HERE/'reports/compile.json'
if prior.exists():(HERE/'reports'/('compile-history-'+hashlib.sha256(prior.read_bytes()).hexdigest()[:12]+'.json')).write_bytes(prior.read_bytes())
for mode in ['ram','sd_readonly','uvc']:
 dest=out/mode;dest.mkdir()
 for f in ['src/tusb.c','src/common/tusb_fifo.c','src/device/usbd.c','src/portable/synopsys/dwc2/dwc2_common.c','src/portable/synopsys/dwc2/dcd_dwc2.c', 'src/class/video/video_device.c' if mode=='uvc' else 'src/class/msc/msc_device.c']:
  source=ROOT/'ThirdParty/tinyusb'/f;obj=dest/(Path(f).stem+'.o');cmd=base+['-DTINYUSB_VIDEO='+str(int(mode=='uvc')),'-c',str(source),'-o',str(obj)]
  p=subprocess.run(cmd,capture_output=True);text=(p.stdout+p.stderr).decode(errors='replace');(dest/(Path(f).stem+'.log')).write_text(text,encoding='utf-8');rows.append(dict(mode=mode,source=f,argv=cmd,exit=p.returncode,output=text,source_sha256=hashlib.sha256(source.read_bytes()).hexdigest()));print(mode,f,p.returncode,flush=True)
error_count=sum(int(n) for x in rows for n in re.findall(r'(\d+) errors',x['output']))
warning_count=sum(int(n) for x in rows for n in re.findall(r'(\d+) warnings',x['output']))
report=dict(error_count=error_count,warning_count=warning_count,full_firmware='BLOCKED / NOT_BUILT',kind='ARMCC5 FreeRTOS component compile only, not firmware build/link',rows=rows,output=str(out),hardware='NOT_TESTED')
(HERE/'reports/compile.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
raise SystemExit(bool(warning_count or error_count or any(x['exit'] for x in rows)))
