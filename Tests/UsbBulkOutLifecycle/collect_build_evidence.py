"""Collect real build artifacts, ELF symbols and immutable source evidence."""
from pathlib import Path
import json,sys,hashlib,subprocess,argparse,shutil
ROOT=Path(__file__).resolve().parents[2];REPORT=Path(__file__).resolve().parent/'reports'
sys.path.insert(0,str(ROOT/'output/UDLM-P1/python_deps'))
from elftools.elf.elffile import ELFFile
ap=argparse.ArgumentParser();ap.add_argument('--tag',default='c1-final05');args=ap.parse_args()
d=json.loads((REPORT/('build-'+args.tag+'.json')).read_text(encoding='utf-8'))
assert not d['original_changed'] and d['rows'][0]['exit']==0
builds=d['rows'][0]['produced_reports'][0]['content']['builds'];rows=[]
names=['HAL_PCD_IRQHandler','Usb_OutRxBegin','Usb_OutEventBegin','Usb_OutNak','USB_Device_EnableBulkOutLifecycle','USB_Device_BeginBulkOutStop','BSP_USB_BeginBulkOutStop']
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for b in builds:
 image=Path(b['artifact']);assert image.is_file() and sha(image)==b['sha256']
 with image.open('rb') as f:
  elf=ELFFile(f);all_symbols={s.name:s['st_shndx'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
 symbols={n:all_symbols.get(n) for n in names}
 log=image.parent/'build.log';dst=REPORT/('firmware-'+args.tag+'-'+b['mode']+'.log');shutil.copyfile(log,dst)
 artifacts={p.suffix:dict(path=str(p.relative_to(ROOT)),bytes=p.stat().st_size,sha256=sha(p)) for p in [image,image.with_suffix('.hex'),image.with_suffix('.map')]}
 assert all(p.is_file() for p in [image,image.with_suffix('.hex'),image.with_suffix('.map')])
 rows.append(dict(**b,artifacts=artifacts,log=str(dst.relative_to(ROOT)),log_sha256=sha(dst),elf_symbols=symbols,enabled_in_application=False))
source_names=['CM7/BSP/bsp_usb.c','CM7/BSP/bsp_usb.h','CM7/Platform/usb_device.c','CM7/Platform/usb_device.h','Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c','Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_pcd.h']
sources={n:sha(ROOT/n) for n in source_names}
for n,h in sources.items():assert sha(Path(d['snapshot'])/n)==h
p=subprocess.run(['D:/32/ARM/ARMCC/Bin/armcc.exe','--vsn'],capture_output=True)
report=dict(tag=args.tag,baseline='3305d4a729e965247b33c95e5558eac576f1c6ef',compiler_version=(p.stdout+p.stderr).decode(errors='replace'),builds=rows,sources=sources,source_matches_snapshot=True,interpretation='HAL hook branch compiled; application Enable is deliberately not connected; symbols alone do not prove runtime binding',hardware='NOT_TESTED')
(REPORT/'build_artifacts.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print([(r['mode'],r['summary'],r['elf_symbols']) for r in rows])
