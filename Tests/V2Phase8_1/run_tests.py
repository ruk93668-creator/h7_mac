"""Execute the board catalog and production validators with ARMCC5/Unicorn."""
from pathlib import Path
import datetime
import json
import os
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
OUT = ROOT / 'output/P8_1' / ('static_' + datetime.datetime.now().strftime('%Y%m%d_%H%M%S_%f'))
OUT.mkdir(parents=True)
os.environ['C1_TEST_OUTPUT_ROOT'] = str(OUT)
sys.path[:0] = [str(ROOT / 'Tests/CameraLifecycleC1'), str(ROOT / 'output/V2Phase1/python_deps')]
import run_tests as arm
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_SP

V2 = ROOT / 'CM7/ApplicationV2'
SOURCES = [
    HERE / 'static_composition_test.c',
    *sorted((V2 / 'Configuration').glob('*.c')),
    V2 / 'Core/media_format.c', V2 / 'MediaGraph/media_graph.c',
    V2 / 'Modules/Camera/camera_format.c', V2 / 'Modules/Camera/camera_module.c',
    V2 / 'Modules/USB/usb_format.c', V2 / 'Modules/USB/usb_module.c', V2 / 'Modules/USB/usb_frame.c',
    V2 / 'Modules/USB/usb_consumer.c',
    V2 / 'Modules/Storage/storage_format.c', V2 / 'Modules/Storage/storage_module.c',
    V2 / 'Modules/Storage/storage_frame.c', V2 / 'Modules/Storage/storage_consumer.c',
    V2 / 'Lifecycle/managed_lifecycle.c',
    V2 / 'Resource/media_resource.c', V2 / 'Resource/media_source.c',
    ROOT / 'CM7/Device/Src/ov5640_device.c', ROOT / 'CM7/Media/Src/camera_capture.c',
    ROOT / 'CM7/Service/usb_uvc_desc.c', ROOT / 'Tests/V2Phase4/native_dependencies.c',
    ROOT / 'CM7/Application/app_camera_lifecycle.c',
    ROOT / 'CM7/Application/app_camera_lifecycle_replay.c',
    ROOT / 'CM7/Application/app_camera_qualification.c',
]

def measure(image, scenario):
    symbols, sections = arm.images[image]
    cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    for address, length in [(0x08000000, 0x100000), (0x20000000, 0x80000), (0x01000000, 0x1000)]:
        cpu.mem_map(address, length)
    for address, data in sections:
        cpu.mem_write(address, data)
    initial_sp = 0x2007fff0
    lowest_sp = initial_sp

    def observe(emulator, _address, _size, _context):
        nonlocal lowest_sp
        lowest_sp = min(lowest_sp, emulator.reg_read(UC_ARM_REG_SP))

    cpu.hook_add(UC_HOOK_CODE, observe)
    cpu.reg_write(UC_ARM_REG_SP, initial_sp)
    cpu.reg_write(UC_ARM_REG_LR, 0x01000001)
    cpu.reg_write(UC_ARM_REG_R0, scenario)
    cpu.emu_start(symbols['Board_Run'] | 1, 0x01000000, count=3000000)
    assert cpu.reg_read(UC_ARM_REG_R0) == 0
    return initial_sp - lowest_sp, list(struct.unpack('<8I', cpu.mem_read(symbols['g_sizes'], 32)))


if __name__ == '__main__':
    arm.cc += ['--split_sections', '-I', ROOT / 'CM7/Device/Components/OV5640']
    rows = []
    for optimization in ('O0', 'O2'):
        arm.cc = ['-' + optimization if arg in ('-O0', '-O2') else arg for arg in arm.cc]
        image = 'board_' + optimization
        arm.build(image, SOURCES, 'Board_Run')
        for scenario in range(17):
            verdict = arm.execute(image, 'Board_Run', scenario)
            row = {'optimization': optimization, 'scenario': scenario, **verdict}
            if verdict['pass']:
                row['stack_bytes'], row['sizeof'] = measure(image, scenario)
            rows.append(row)
            print(row, flush=True)
    result = {
        'head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
        'rows': rows,
    }
    (OUT / 'results.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(OUT)
    raise SystemExit(any(not row['pass'] for row in rows))
