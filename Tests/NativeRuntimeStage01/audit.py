"""Stage01 source boundaries and linked firmware audit (requires formal CM7 AXF)."""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
BASE = '8d3741a74692ec7f16a78884d86ecfcf222260d3'
sys.path.insert(0, str(ROOT / 'Tests/OledAsyncAcceptance/python_deps'))
from elftools.elf.elffile import ELFFile

def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT)

def source(name):
    data = (ROOT / name).read_bytes()
    try:
        return data.decode('utf-8')
    except UnicodeDecodeError:
        return data.decode('gbk')

checks = []
def check(name, passed, detail=None):
    checks.append({'name': name, 'passed': bool(passed), 'detail': detail})

protected = ['CM4', 'Drivers', 'Board', 'Middlewares', 'CM7/BSP', 'CM7/Device', 'CM7/Media',
             'CM7/Service/usb_uvc_class.c', 'CM7/Service/usb_uvc.h',
             'CM7/Service/usb_uvc_payload.c', 'CM7/Service/usb_uvc_payload.h',
             'CM7/Service/usb_uvc_desc.c']
unchanged = []
for path in protected:
    unchanged.extend(git('diff', BASE, '--name-only', '--', path).decode().splitlines())
check('Protected hardware, CM4, UVC and official code unchanged', not unchanged, unchanged)
check('Historical tests and assertions unchanged', not git('diff', BASE, '--', 'Tests',
      ':!Tests/NativeRuntimeStage01').strip())

bad = []
for folder in ['CM7/BSP', 'CM7/Device', 'CM7/Media', 'CM7/Service', 'Common']:
    for p in (ROOT / folder).rglob('*'):
        if p.suffix not in ['.c', '.h']:
            continue
        text = source(p.relative_to(ROOT))
        if re.search(r'#\s*include\s*[<"](?:FreeRTOS|task|queue|semphr|event_groups)\.h|\b(?:TaskHandle_t|StaticTask_t|TickType_t)\b', text):
            bad.append(p.relative_to(ROOT).as_posix())
check('Lower layers contain no RTOS headers or task types', not bad, bad)
main = source('CM7/Core/Src/main.c')
check('Create -> Initcall -> record result -> scheduler',
      main.index('App_TasksCreate()') < main.index('Framework_InitRuntime()') <
      main.index('App_TasksRecordInitialization(') < main.index('Platform_RtosStart()'))
check('No CM7 global runtime polling', not re.search(r'\b(?:Main_Run|App_Run|Framework_Run)\s*\(', main))
platform = source('CM7/Platform/platform_rtos.c')
check('Platform owns no business task', all(x not in platform for x in
      ['xTaskCreateStatic', 'LegacyVideo', 'Rtos_LegacyTask', 's_run_once']))
tasks = '\n'.join(source(p.relative_to(ROOT)) for p in (ROOT/'CM7/Application/Tasks').glob('*.c'))
check('Stage01 tasks do not capture or borrow frames', not re.search(
      r'\b(?:CameraCapture_\w+|App_Camera\w+|USB_UVC_\w+|App_UvcBridge\w+)\s*\(', tasks))
usb = source('CM7/BSP/bsp_usb.c')
check('Validated Sleep clock configuration retained', all(x in usb for x in
      ['__HAL_RCC_USB2_OTG_FS_CLK_SLEEP_ENABLE();', '__HAL_RCC_USB2_OTG_FS_ULPI_CLK_SLEEP_DISABLE();']))

axf = ROOT/'MDK-ARM/h7_mac_CM7/h7_mac_CM7.axf'
with axf.open('rb') as f:
    elf = ELFFile(f)
    symbols = {s.name: (s['st_value'], s['st_size']) for s in elf.get_section_by_name('.symtab').iter_symbols()}
    check('Three task entries linked', all(x in symbols for x in ['CameraTask_Run', 'UsbTask_Run', 'ControlTask_Run']))
    sizes = {k: symbols.get(k, (0, 0))[1] for k in ['s_camera_stack', 's_usb_stack', 's_control_stack', 's_idle_stack']}
    check('Static task stack sizes', list(sizes.values()) == [4096, 8192, 3072, 1024], sizes)
    check('No Legacy/global runtime entry linked', not any(x in symbols for x in
          ['Rtos_LegacyTask', 'Main_Run', 'App_Run', 'Framework_Run']))
    vectors = {}
    for index, name in [(11, 'SVC_Handler'), (14, 'PendSV_Handler'), (15, 'SysTick_Handler')]:
        addr = 0x08000000 + 4 * index
        for s in elf.iter_sections():
            if s['sh_addr'] <= addr < s['sh_addr'] + s['sh_size'] and s['sh_type'] == 'SHT_PROGBITS':
                value = int.from_bytes(s.data()[addr-s['sh_addr']:addr-s['sh_addr']+4], 'little')
                vectors[name] = hex(value)
                check(name + ' vector', name in symbols and (value & ~1) == (symbols[name][0] & ~1), hex(value))
                break
        else:
            check(name + ' vector', False, 'missing vector bytes')
check('CM7 and CM4 full rebuild clean', all('0 Error(s), 0 Warning(s).' in
      (HERE/f'h7_mac_{core}_rebuild.log').read_text(errors='replace') for core in ['CM7', 'CM4']))
result = {'baseline': BASE, 'firmware_sha256': hashlib.sha256(axf.read_bytes()).hexdigest(),
          'checks': checks, 'passed': sum(x['passed'] for x in checks),
          'failed': sum(not x['passed'] for x in checks), 'hardware_verified': False}
(HERE/'audit_results.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
for row in checks:
    print('PASS' if row['passed'] else 'FAIL', row['name'], row['detail'] or '')
raise SystemExit(bool(result['failed']))
