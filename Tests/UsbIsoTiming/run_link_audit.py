"""Audit the actual capture AXF and generate its exact Keil export addresses."""
from pathlib import Path
import subprocess,hashlib,re,json
from elftools.elf.elffile import ELFFile
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
axf=ROOT/'MDK-ARM/h7_mac_CM7/h7_mac_CM7.axf'
with axf.open('rb') as f:
    e=ELFFile(f);sy={x.name:(x['st_value'],x['st_size']) for x in e.get_section_by_name('.symtab').iter_symbols()}
checks=dict(trace_buffer=sy.get('s_usb_iso_records')==(0x24020000,24576),metadata_size=sy.get('g_usb_iso_timing',(0,0))[1]==64,metadata_dtcm=0x20000000<=sy.get('g_usb_iso_timing',(0,0))[0]<0x20020000,sd_buffer_unchanged=sy.get('s_stress_buffer')==(0x24010000,33288),idma_unchanged=sy.get('g_sdmmc_dma_buffer')==(0x24000000,32768))
for name in ['sd_block_probe.c','sd_block_probe.h']:
    rel='Tests/SdmmcStage08B/'+name;checks['old_c_'+name]=(ROOT/rel).read_bytes()==subprocess.check_output(['git','show','d030531:'+rel])
for rel in ['CM7/BSP/bsp_usb.c','CM7/Service/usb_uvc_class.c','CM7/Application/Tasks/usb_task.c']:
    def text(data):
        try:return data.decode('utf-8-sig')
        except UnicodeDecodeError:return data.decode('gbk')
    old=text(subprocess.check_output(['git','show','HEAD:'+rel]));new=text((ROOT/rel).read_bytes())
    stripped=re.sub(r'#if defined\(APP_USB_ISO_TIMING_TEST_ENABLED\) && APP_USB_ISO_TIMING_TEST_ENABLED\s*\n.*?#endif','',new,flags=re.S)
    checks['only_trace_blocks_'+Path(rel).name]=''.join(old.split())==''.join(stripped.split())
checks['hal_ll_core_unchanged']=not subprocess.check_output(['git','diff','--name-only','HEAD','--','Drivers','Middlewares/ST/STM32_USB_Device_Library','CM4','CM7/Media','CM7/Device','CM7/Application/app_camera.c','CM7/Application/app_uvc_bridge.c']).strip()
report=dict(checks=checks,summary=dict(total=len(checks),passed=sum(checks.values())),symbols={k:dict(address=hex(sy[k][0]),size=sy[k][1]) for k in ['s_usb_iso_records','g_usb_iso_timing','s_stress_buffer','g_sdmmc_dma_buffer']},axf_sha256=hashlib.sha256(axf.read_bytes()).hexdigest(),hardware_timing_verified=False)
(HERE/'link_audit.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
if all(checks.values()):
    addr=sy['g_usb_iso_timing'][0]
    (HERE/'dump_trace.ini').write_text('SAVE usb_iso_trace.hex 0x24020000,0x24025FFF\nSAVE usb_iso_meta.hex 0x%08X,0x%08X\n'%(addr,addr+63),encoding='ascii')
print(report['summary']);print(report['symbols'])
raise SystemExit(not all(checks.values()))
