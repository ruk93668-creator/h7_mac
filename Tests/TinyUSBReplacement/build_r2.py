"""Generate an isolated complete Keil target; never write the default project/RTE."""
from pathlib import Path
import xml.etree.ElementTree as E
import subprocess,json,hashlib,re,time
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/TinyUSBReplacement'/('r2-firmware-'+str(time.time_ns()));OUT.mkdir(parents=True)
original=ROOT/'MDK-ARM/h7_mac.uvprojx';before=original.read_bytes()
preserve=[original,ROOT/'MDK-ARM/h7_mac.uvoptx',ROOT/'MDK-ARM/RTE/_h7_mac_CM7/RTE_Components.h']
preserved={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in preserve}
r=E.fromstring(before);targets=r.find('Targets');t=next(t for t in targets if t.findtext('TargetName')=='h7_mac_CM7')
for other in list(targets):
 if other is not t:targets.remove(other)
t.find('TargetName').text='TinyUSB_RAM_CM7'
c=t.find('TargetOption/TargetCommonOption')
for k,v in dict(OutputDirectory=str(OUT)+'/',OutputName='h7_mac_CM7_TinyUSB_RAM',ListingPath=str(OUT)+'/',CreateHexFile='1',BrowseInformation='0').items():c.find(k).text=v
vc=t.find('TargetOption/TargetArmAds/Cads/VariousControls');vc.find('Define').text='CORE_CM7,USE_HAL_DRIVER,STM32H747xx,USE_PWR_LDO_SUPPLY,TINYUSB_RAM_EXPERIMENT=1'
vc.find('IncludePath').text += ';../CM7/TinyUSB;../ThirdParty/tinyusb/src'
vc.find('MiscControls').text='' # no old PCD/SD/DCMI forced include
ld=t.find('TargetOption/TargetArmAds/LDads');ld.find('ScatterFile').text='./tinyusb_ram_CM7.sct';ld.find('Misc').text=''
# Keep HAL dependencies, exclude only PCD/USB owners, and retain official CM7 RTOS/startup.
files=[]
for g in t.findall('Groups/Group'):
 for f in g.findall('Files/File'):
  name=f.findtext('FilePath').replace('\\','/')
  if 'STM32H7xx_HAL_Driver/Src/' in name and not any(x in name for x in ['hal_pcd','ll_usb']):files.append(name)
files += ['startup_stm32h747xx_CM7.s','../Common/Src/system_stm32h7xx_dualcore_boot_cm4_cm7.c']
files += ['../CM7/Core/Src/'+f for f in ['main.c','stm32h7xx_it.c','stm32h7xx_hal_msp.c']]
files += ['../CM7/Platform/'+f for f in ['platform_rtos.c','platform_hal_timebase.c','platform_sync.c']]
files += ['../Middlewares/Third_Party/FreeRTOS/Source/'+f for f in ['tasks.c','list.c','queue.c','portable/RVDS/ARM_CM7/r0p1/port.c']]
files += ['../CM7/Application/Tasks/usb_task.c','../CM7/Service/usb_msc_ram.c']
files += ['../CM7/TinyUSB/'+f for f in ['tinyusb_experiment.c','tinyusb_ram.c','usb_descriptors.c']]
files += ['../ThirdParty/tinyusb/src/'+f for f in ['tusb.c','common/tusb_fifo.c','device/usbd.c','class/msc/msc_device.c','portable/synopsys/dwc2/dcd_dwc2.c','portable/synopsys/dwc2/dwc2_common.c']]
groups=t.find('Groups');groups.clear();g=E.SubElement(groups,'Group');E.SubElement(g,'GroupName').text='Isolated TinyUSB RAM CM7';fs=E.SubElement(g,'Files')
for name in files:
 f=E.SubElement(fs,'File');E.SubElement(f,'FileName').text=Path(name).name;E.SubElement(f,'FileType').text='2' if name.endswith('.s') else '1';E.SubElement(f,'FilePath').text=name
# Never run flashing/programming hooks inherited from the base target.
for x in t.findall('.//RunUserProg1')+t.findall('.//RunUserProg2'):x.text='0'
for x in t.findall('.//UpdateFlashBeforeDebugging'):x.text='0'
for x in t.findall('.//Flash4'):x.text=''
project=ROOT/'MDK-ARM/h7_mac_TinyUSB_RAM.uvprojx';E.indent(r,space='  ');E.ElementTree(r).write(project,encoding='utf-8',xml_declaration=True)
log=OUT/'build.log';cmd=['D:/32/UV4/UV4.exe','-j0','-r',str(project),'-t','TinyUSB_RAM_CM7','-o',str(log)]
si=subprocess.STARTUPINFO();si.dwFlags|=subprocess.STARTF_USESHOWWINDOW;si.wShowWindow=0
p=subprocess.run(cmd,cwd=ROOT/'MDK-ARM',startupinfo=si,creationflags=subprocess.CREATE_NO_WINDOW)
text=log.read_text(errors='replace');print(text[-6000:]);assert original.read_bytes()==before
# uVision may emit .axf/.map despite OutputName in a generated target. Preserve
# raw outputs and provide stable, hashed names for review (no download).
import shutil
for ext in ['axf','map','hex']:
 src=OUT/('.'+ext)
 if src.exists():shutil.copy2(src,OUT/('h7_mac_CM7_TinyUSB_RAM.'+ext))
m=re.search(r'(\d+) Error\(s\), (\d+) Warning\(s\)',text)
for kept_path in preserve:assert hashlib.sha256(kept_path.read_bytes()).hexdigest()==preserved[str(kept_path.relative_to(ROOT))]
(HERE/'reports'/('r2-build-'+OUT.name+'.log')).write_bytes(log.read_bytes())
report=dict(source_sha256={str((ROOT/'MDK-ARM'/f).resolve().relative_to(ROOT)):hashlib.sha256((ROOT/'MDK-ARM'/f).read_bytes()).hexdigest() for f in files},preserved=preserved,project_sha256=hashlib.sha256(project.read_bytes()).hexdigest(),argv=cmd,exit=p.returncode,summary=m.group() if m else None,output=str(OUT),sources=files,default_project_sha256=hashlib.sha256(before).hexdigest(),artifacts={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in OUT.iterdir() if f.suffix in ['.axf','.map','.hex']},hardware='NOT_TESTED')
prior=HERE/'reports/r2-build.json'
if prior.exists():(HERE/'reports'/('r2-build-history-'+hashlib.sha256(prior.read_bytes()).hexdigest()[:12]+'.json')).write_bytes(prior.read_bytes())
(HERE/'reports/r2-build.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
raise SystemExit(p.returncode)
