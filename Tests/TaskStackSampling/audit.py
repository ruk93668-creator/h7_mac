"""Audit source boundaries and the actual no-recorder firmware image."""
from pathlib import Path
import subprocess,re,json,sys
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'Tests/OledAsyncAcceptance/python_deps'))
from elftools.elf.elffile import ELFFile

def text(b):
    try:return b.decode('utf-8-sig')
    except UnicodeDecodeError:return b.decode('gbk')
def normalize(s):
    s=re.sub(r'#if defined\(APP_USB_ISO_TIMING_TEST_ENABLED\) && APP_USB_ISO_TIMING_TEST_ENABLED[^\n]*\n.*?#endif','',s,flags=re.S)
    s=re.sub(r'//[^\n]*','',s)
    s=re.sub(r'#define \w+_TASK_STACK_SAMPLE_MS 1000U','',s)
    s=re.sub(r'uint32_t stack_tick = Platform_TimeGetMs\(\);\s*uint32_t stack_now;','',s)
    p=r'stack_now = Platform_TimeGetMs\(\);\s*if \(\(uint32_t\)\(stack_now - stack_tick\) >= \w+\)\s*\{\s*stack_tick = stack_now;\s*(g_\w+_task_debug.stack_free_words = uxTaskGetStackHighWaterMark\(NULL\);)\s*\}'
    s=re.sub(p,r'\1',s)
    if 'SdBlockTest_' in s:
        s=re.sub(r'static void SdBlockTest_SampleStack\(void\)\s*\{.*?\n\}','',s,flags=re.S)
        s=s.replace('static void SdBlockTest_SampleStack(void);','').replace('static uint32_t s_stack_sample_tick;','').replace('s_stack_sample_tick = Platform_TimeGetMs();','')
        s=s.replace('SdBlockTest_SampleStack();','g_sd_block_test.stack_free_words = uxTaskGetStackHighWaterMark(NULL);')
    return ''.join(s.split())
files=[f'CM7/Application/Tasks/{n}_task.c' for n in ['camera','usb','control','ipc']]+['Tests/SdmmcStage08B/sd_block_probe.c','CM7/BSP/bsp_usb.c','CM7/Platform/usb_device.c','CM7/Service/usb_uvc_class.c']
checks={}
for rel in files:
    old=text(subprocess.check_output(['git','show','d1654c9:'+rel],cwd=ROOT));current=text((ROOT/rel).read_bytes())
    checks['only_requested_changes_'+Path(rel).name]=normalize(old)==normalize(current)
checks['no_timing_hooks']=all(not any(word in text((ROOT/r).read_bytes()) for word in ['UsbIsoTiming','USB_ISO_TIMING','usb_iso_timing']) for r in files)
project=(ROOT/'MDK-ARM/h7_mac.uvprojx').read_text(encoding='utf-8')
for macro in ['APP_SD_BLOCK_TEST_ENABLED=1','APP_SD_BLOCK_STRESS_TEST_ENABLED=1','APP_SD_BLOCK_WRITE_TEST_ENABLED=0','APP_SDMMC_TEST_ENABLED=0','APP_SDMMC_DETECT_ONLY=0']:
    checks[macro]=macro in project
checks['no_project_recorder']='UsbIsoTiming' not in project and 'USB_ISO_TIMING' not in project and 'usb_iso_timing' not in project
checks['protected_modules_unchanged']=not subprocess.check_output(['git','diff','--name-only','d1654c9','--','Drivers','Middlewares','CM4','CM7/Media','CM7/Device','CM7/Application/app_uvc_bridge.c','CM7/Application/app_camera.c'],cwd=ROOT).strip()
with (ROOT/'MDK-ARM/h7_mac_CM7/h7_mac_CM7.axf').open('rb') as f:
    elf=ELFFile(f);sy={x.name:(x['st_value'],x['st_size']) for x in elf.get_section_by_name('.symtab').iter_symbols()}
    checks['no_linked_recorder']=not any('UsbIsoTiming' in name or name in ['s_usb_iso_records','g_usb_iso_timing'] for name in sy)
    checks['sd_buffers_unchanged']=sy.get('s_stress_buffer')==(0x24010000,33288) and sy.get('g_sdmmc_dma_buffer')==(0x24000000,32768)
report=dict(total=len(checks),passed=sum(checks.values()),checks=checks,hardware_fps_verified=False)
(HERE/'audit_results.json').write_text(json.dumps(report,indent=2)+'\n');print(report);assert all(checks.values())
