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
OUT = ROOT / 'output/V2Phase7/joint'
OUT.mkdir(parents=True, exist_ok=True)
os.environ['C1_TEST_OUTPUT_ROOT'] = str(OUT)
sys.path.insert(0, str(ROOT / 'Tests/CameraLifecycleC1'))
sys.path.insert(0, str(ROOT / 'output/V2Phase1/python_deps'))
import run_tests as arm
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0

V2 = ROOT / 'CM7/ApplicationV2'
NEW = V2 / 'Core/Runtime'
SOURCES = [HERE / 'control_test.c', *sorted(NEW.glob('control_*.c')),
           V2 / 'Core/media_format.c', V2 / 'Configuration/v2_registry.c',
           V2 / 'Configuration/v2_composition.c', V2 / 'MediaGraph/media_graph.c',
           V2 / 'Lifecycle/managed_lifecycle.c', V2 / 'Supervisor/execution_plan.c',
           V2 / 'Supervisor/execution_manager.c']
base_run = arm.run
def logged(cmd):
    cmd = list(cmd)
    if Path(str(cmd[0])).name.lower() == 'armlink.exe':
        axf = Path(cmd[cmd.index('-o') + 1])
        cmd += ['--map', '--list', axf.with_suffix('.map'), '--callgraph', '--symbols']
    return base_run(cmd)

arm.run = logged
arm.cc += ['--split_sections']
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
    emulator.emu_start(symbols['Control_Run'] | 1, 0x01000000, count=3000000)
    assert emulator.reg_read(UC_ARM_REG_R0) == 0, case
    layout = list(struct.unpack('<12I', emulator.mem_read(symbols['g_sizes'], 48)))
    return {'id': case, 'stack_bytes': initial - low, 'layout': layout}

negatives = []
MUTANTS = [
    ('wrong_peer_access', 'control_service.c', 'id && peer_equal(p, &s->sessions[i].peer)', 'id', 2),
    ('timeout_discards_execution', 'control_service.c',
     'if (v->reply.phase != CS_EXECUTING)\n        {', 'if (1)\n        {', 12),
    ('candidate_as_execution', 'control_service.c',
     'v->reply.effect = CS_EFFECT_CANDIDATE;', 'v->reply.effect = CS_EFFECT_EXECUTION;', 3),
    ('unconsumed_record_eviction', 'control_service.c', 'if (v->delivered && v->reply.id.ticket < oldest)',
     'if (v->reply.id.ticket < oldest)', 5),
]
rows = []
resources = {}
for opt in ['O0', 'O2']:
    arm.cc = ['-' + opt if x in ['-O0', '-O2'] else x for x in arm.cc]
    name = 'joint_' + opt
    arm.build(name, SOURCES, 'Control_Run')
    for case in range(26):
        result = arm.execute(name, 'Control_Run', case)
        row = {'id': case, 'opt': opt, **result}
        rows.append(row)
        print(row, flush=True)
    objects = {}
    for idx, path in enumerate(SOURCES):
        if path.parent != NEW:
            continue
        with (OUT / (name + str(idx) + '.o')).open('rb') as f:
            sizes = dict(RO=0, RW=0, ZI=0)
            for section in ELFFile(f).iter_sections():
                if section['sh_flags'] & 2:
                    key = 'ZI' if section['sh_type'] == 'SHT_NOBITS' else 'RW' if section['sh_flags'] & 1 else 'RO'
                    sizes[key] += section['sh_size']
            objects[path.name] = sizes
    resources[opt] = {'objects': objects,
                      'layout_order': ['CSService', 'CSExecutionEndpoint', 'CSAdapter', 'CSRequest',
                                       'CSReply', 'CSIngress', 'CSEgress', 'CSService.scratch',
                                       'IpcShared', 'CSExecution', 'CSExecClient', 'CSExecServer'],
                      'probes': [measure(name, case) for case in [0, 3, 4, 5, 12, 16, 17, 21, 23, 24]
                                 if rows[-26 + case]['pass']]}

    arm.cc += ['-I', NEW]
    for mutant, filename, before, after, case in MUTANTS:
        original = NEW / filename
        text = original.read_text(encoding='utf-8')
        actual_count = text.count(before)
        assert actual_count == 1, before
        mutation = OUT / (mutant + '.c')
        mutation.write_text(text.replace(before, after), encoding='utf-8')
        image = mutant + '_' + opt
        arm.build(image, [mutation if f == original else f for f in SOURCES], 'Control_Run')
        verdict = arm.execute(image, 'Control_Run', case)
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
