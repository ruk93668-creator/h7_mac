"""Check that the normal Keil image contains SD support without acceptance probes."""
from pathlib import Path
import json,sys,xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'Tests/OledAsyncAcceptance/python_deps'))
from elftools.elf.elffile import ELFFile
project=ROOT/'MDK-ARM/h7_mac.uvprojx';tree=ET.parse(project).getroot();checks={};targets=tree.findall('./Targets/Target')
for target in targets:
    name=target.findtext('TargetName');files=[x.text or '' for x in target.findall('.//FilePath')]
    checks[name+'_no_probes']=not any('sdmmc_probe.c' in x or 'sd_block_probe.c' in x for x in files)
    defines=','.join(x.text or '' for x in target.findall('.//Define'))
    checks[name+'_no_test_defines']=not any(x.strip().startswith(('APP_SDMMC_','APP_SD_BLOCK_','SD_TEST_')) for x in defines.split(','))
cm7=next(x for x in targets if x.findtext('TargetName')=='h7_mac_CM7');files=[x.text or '' for x in cm7.findall('.//FilePath')]
for filename in ['bsp_sdmmc.c','bsp_sdmmc_memory.c','sd_block.c','stm32h7xx_hal_sd.c','stm32h7xx_ll_sdmmc.c']:
    checks['compiled_'+filename]=any(x.replace('\\','/').endswith('/'+filename) for x in files)
sct=(ROOT/'MDK-ARM/h7_mac_CM7_custom.sct').read_bytes().decode('gbk')
checks['no_stress_scatter']='RW_SD_STRESS_CPU' not in sct and '.sd_stress_cpu' not in sct
checks['idma_scatter_retained']='RW_SDMMC_DMA 0x24000000 UNINIT 0x00008000' in sct
axf=ROOT/'MDK-ARM/h7_mac_CM7/h7_mac_CM7.axf'
with axf.open('rb') as f:
    elf=ELFFile(f);sy={x.name:(x['st_value'],x['st_size']) for x in elf.get_section_by_name('.symtab').iter_symbols()}
checks['no_sd_probe_symbols']=not any(n.startswith(('SdBlockTest_','SdmmcTest_')) or n in ['g_sd_block_test','g_sdmmc_test','s_stress_buffer','s_test_tcb','s_test_stack'] for n in sy)
checks['idma_buffer_retained']=sy.get('g_sdmmc_dma_buffer')==(0x24000000,32768)
checks['sd_platform_prepare_retained']='SdBlock_Prepare' in sy
checks['no_usb_sampler']=not any('UsbIsoTiming' in n or n in ['g_usb_iso_timing','s_usb_iso_records'] for n in sy)
report=dict(total=len(checks),passed=sum(checks.values()),checks=checks,hardware_verified=False)
(HERE/'normal_firmware_checks.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(report);assert all(checks.values())
