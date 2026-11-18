"""ARMCC5 Cortex-M7 joint acceptance in Unicorn. Outputs never replace historical evidence."""
from pathlib import Path
import hashlib
import json
import os
import struct
import sys

from subprocess import check_output

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
OUT = ROOT / 'output/V2Phase5/joint'
OUT.mkdir(parents=True, exist_ok=True)
os.environ['C1_TEST_OUTPUT_ROOT'] = str(OUT)
sys.path.insert(0, str(ROOT / 'Tests/CameraLifecycleC1'))
sys.path.insert(0, str(ROOT / 'output/V2Phase1/python_deps'))
import run_tests as arm
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0

V2 = ROOT / 'CM7/ApplicationV2'
SOURCES = [
    HERE / 'usb_joint.c', *sorted((V2 / 'Modules/USB').glob('*.c')),
    V2 / 'Modules/Camera/camera_format.c', V2 / 'Modules/Camera/camera_module.c',
    V2 / 'Configuration/v2_registry.c',
    V2 / 'Core/media_format.c',
    V2 / 'Lifecycle/managed_lifecycle.c',
    V2 / 'Resource/media_source.c', V2 / 'Resource/media_resource.c',
    ROOT / 'Tests/V2Phase4/native_dependencies.c',
    ROOT / 'CM7/Device/Src/ov5640_device.c',
    ROOT / 'CM7/Media/Src/camera_capture.c',
    ROOT / 'CM7/Service/usb_uvc_desc.c',
    *(ROOT / 'CM7/Application' / (name + '.c') for name in
      ['app_camera_lifecycle', 'app_camera_lifecycle_replay', 'app_camera_qualification']),
]
base_run = arm.run
def logged(cmd):
    cmd = list(cmd)
    if Path(str(cmd[0])).name.lower() == 'armlink.exe':
        axf = Path(cmd[cmd.index('-o') + 1])
        cmd += ['--map', '--list', axf.with_suffix('.map'), '--callgraph', '--symbols']
    return base_run(cmd)

arm.run = logged
arm.cc += ['--split_sections', '-I', ROOT / 'CM7/Device/Components/OV5640']
def measure(name, case):
    symbols, sections = arm.images[name]
    emulator = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    for addr, size in [(0x08000000, 0x100000), (0x20000000, 0x80000),
                       (0xE000E000, 0x2000), (0x01000000, 0x1000)]:
        emulator.mem_map(addr, size)
    for addr, data in sections:
        emulator.mem_write(addr, data)
    initial = 0x2007fff0
    low = initial

    def visit(cpu, addr, size, _):
        nonlocal low
        low = min(low, cpu.reg_read(UC_ARM_REG_SP))

    emulator.hook_add(UC_HOOK_CODE, visit)
    emulator.reg_write(UC_ARM_REG_SP, initial)
    emulator.reg_write(UC_ARM_REG_LR, 0x01000001)
    emulator.reg_write(UC_ARM_REG_R0, case)
    emulator.emu_start(symbols['USB_Run'] | 1, 0x01000000, count=3000000)
    assert emulator.reg_read(UC_ARM_REG_R0) == 0, case
    layout = list(struct.unpack('<8I', emulator.mem_read(symbols['g_sizes'], 32)))
    return {'id': case, 'stack_bytes': initial - low, 'layout': layout}

negatives = []
MUTANTS = [
    ('skip_dma_gate', 'usb_frame.c',
     'z = f->ops->readers_safe(f->port, &f->request, &f->data);', 'z = SYS_OK;', 0),
    ('false_release', 'usb_consumer.c', 'reply.completion_flags & APP_LIFECYCLE_QUALIFICATION_RELEASED',
     '1U', 8),
    ('overwrite_first_fault', 'usb_module.c', 'complete(u, z, LC_ERROR);',
     'complete(u, SYS_OK, LC_INACTIVE);', 6),
]
rows = []
resources = {}
for opt in ['O0', 'O2']:
    arm.cc = ['-' + opt if x in ['-O0', '-O2'] else x for x in arm.cc]
    name = 'joint_' + opt
    arm.build(name, SOURCES, 'USB_Run')
    for case in range(19):
        result = arm.execute(name, 'USB_Run', case)
        row = {'id': case, 'opt': opt, **result}
        rows.append(row)
        print(row, flush=True)
    objects = {}
    for idx, path in enumerate(SOURCES):
        if path.parent.name != 'USB':
            continue
        with (OUT / (name + str(idx) + '.o')).open('rb') as f:
            sizes = dict(RO=0, RW=0, ZI=0)
            for section in ELFFile(f).iter_sections():
                if section['sh_flags'] & 2:
                    key = 'ZI' if section['sh_type'] == 'SHT_NOBITS' else 'RW' if section['sh_flags'] & 1 else 'RO'
                    sizes[key] += section['sh_size']
            objects[path.name] = sizes
    resources[opt] = {'objects': objects, 'layout_order': ['VUSB', 'VUSBEndpoint', 'VUSBClient',
                      'AppLifecycleMessage', 'VUSBFrame', 'VUSBConsumer', 'VUSB.consumer',
                      'VUSB.frame'], 'probes': [measure(name, case) for case in [0, 1, 6, 8, 9, 14, 16, 18]]}

    arm.cc += ['-I', V2 / 'Modules/USB']
    for mutant, filename, before, after, case in MUTANTS:
        original = V2 / 'Modules/USB' / filename
        text = original.read_text(encoding='utf-8')
        actual_count = text.count(before)
        assert actual_count and (mutant != 'skip_dma_gate' or actual_count == 2), before
        mutation = OUT / (mutant + '.c')
        mutation.write_text(text.replace(before, after), encoding='utf-8')
        image = mutant + '_' + opt
        arm.build(image, [mutation if f == original else f for f in SOURCES], 'USB_Run')
        verdict = arm.execute(image, 'USB_Run', case)
        negatives.append({'mutant': mutant, 'opt': opt, 'id': case,
                          'detected': not verdict['pass'] and bool(verdict['failure_line']) and
                                      not verdict['error'], 'raw': verdict})
    del arm.cc[-2:]
report = {'source_head': check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT).decode().strip(),
          'cases': rows, 'resources': resources, 'negative_controls': negatives,
          'source_sha256': {str(p.relative_to(ROOT)).replace(chr(92), '/'): hashlib.sha256(p.read_bytes()).hexdigest() for p in SOURCES},
          'summary': {'total': len(rows), 'pass': sum(bool(x['pass']) for x in rows)}}
(OUT / 'results.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print(report['summary'], flush=True)
print('negative controls', sum(x['detected'] for x in negatives), '/', len(negatives))
raise SystemExit(any(not x['pass'] for x in rows) or any(not x['detected'] for x in negatives))
