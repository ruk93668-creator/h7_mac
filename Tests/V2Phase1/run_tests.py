"""ARMCC5 O0/O2 Cortex-M7 tests. Only pure V2 code and mock descriptors."""
from pathlib import Path
import hashlib
import json
import os
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
OUT = ROOT / 'output/V2Phase1/tests'
OUT.mkdir(parents=True, exist_ok=True)
os.environ['C1_TEST_OUTPUT_ROOT'] = str(OUT)
sys.path.insert(0, str(ROOT / 'Tests/CameraLifecycleC1'))
sys.path.insert(0, str(ROOT / 'output/V2Phase1/python_deps'))
import run_tests as arm
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0
from elftools.elf.elffile import ELFFile

# Extend link evidence locally; the historical helper is not edited.
original_run = arm.run

def run(command):
    command = list(command)
    if Path(str(command[0])).name.lower() == 'armlink.exe':
        axf = Path(command[command.index('-o') + 1])
        command += ['--map', '--list', axf.with_suffix('.map'), '--callgraph', '--symbols']
    return original_run(command)

arm.run = run
sources = [HERE / 'phase1_test.c', *sorted((ROOT / 'CM7/ApplicationV2').rglob('*.c'))]
size_names = ['CoreMediaFormat', 'GraphPipelineDesc', 'GraphSelection', 'GraphValidatedView',
              'GraphScratch', 'V2ModuleDesc', 'V2Resolution', 'V2ResolverScratch',
              'V2CompositionCandidate', 'V2CompositionScratch', 'GraphCaps', 'GraphEntityDesc',
              'GraphPadDesc', 'GraphLinkDesc', 'GraphDesc', 'GraphWorkspace', 'V2Provided',
              'V2Requirement', 'V2Registry', 'V2Issue', 'GraphIssue', 'V2EntityBinding',
              'V2Desired', 'GraphEntityOps', 'GraphFrameRate', 'GraphSinkPolicy', 'AppMessage_t', 'AppLifecycleMessage_t', 'CameraFrame_t', 'V2Status']

def probe(name, case):
    symbols, sections = arm.images[name]
    emulator = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    for address, size in [(0x08000000, 0x100000), (0x20000000, 0x80000),
                          (0xE000E000, 0x2000), (0x01000000, 0x1000)]:
        emulator.mem_map(address, size)
    for address, data in sections:
        emulator.mem_write(address, data)
    start_sp = 0x2007FFF0
    minimum = [start_sp]
    def observe(uc, address, size, user):
        minimum[0] = min(minimum[0], uc.reg_read(UC_ARM_REG_SP))
    emulator.hook_add(UC_HOOK_CODE, observe)
    emulator.reg_write(UC_ARM_REG_SP, start_sp)
    emulator.reg_write(UC_ARM_REG_LR, 0x01000001)
    emulator.reg_write(UC_ARM_REG_R0, case)
    emulator.emu_start(symbols['V2_Phase1Run'] | 1, 0x01000000, count=3000000)
    return_code = emulator.reg_read(UC_ARM_REG_R0)
    sizes = struct.unpack('<30I', emulator.mem_read(symbols['g_sizes'], 30 * 4)) if case == 0 else None
    return {'case': case, 'return': return_code, 'measured_mock_test_stack_bytes': start_sp-minimum[0],
            'sizeof': dict(zip(size_names, sizes)) if sizes else None}

def category(case):
    if case <= 18 or case in [111,112]: return 'Core'
    if case <= 35: return 'Registry'
    if case <= 52 or case in [100,107,110,120]: return 'Resolver'
    if case <= 88 or case in [101,102,103,104,105,106,108,113,114,115,116,117,118,121,122,123,124]: return 'Graph_Format'
    return 'Composition'

rows, resources = [], {}
for opt in ['O0', 'O2']:
    arm.cc = [('-'+opt if x in ['-O0','-O2'] else x) for x in arm.cc]
    arm.build('phase1_'+opt, sources, 'V2_Phase1Run')
    for case in range(125):
        result = arm.execute('phase1_'+opt, 'V2_Phase1Run', case)
        rows.append({'id': case, 'optimization': opt, 'group': category(case), **result})
        print(opt, case, 'PASS' if result['pass'] else 'FAIL', result['failure_line'], result['error'], flush=True)
    probes = [probe('phase1_'+opt, case) for case in [0,89,100,102,103]]
    objects = []
    for index, source in enumerate(sources[1:], 1):
        obj = OUT / ('phase1_'+opt+str(index)+'.o')
        with obj.open('rb') as f:
            elf = ELFFile(f)
            totals = {'RO':0, 'RW':0, 'ZI':0}
            for section in elf.iter_sections():
                if section['sh_flags'] & 2:
                    key = 'ZI' if section['sh_type']=='SHT_NOBITS' else ('RW' if section['sh_flags'] & 1 else 'RO')
                    totals[key] += section['sh_size']
        objects.append({'source':str(source.relative_to(ROOT)), **totals})
    resources[opt] = {'probes':probes, 'objects':objects}
summary = {'total':len(rows), 'pass':sum(x['pass'] for x in rows), 'fail':sum(not x['pass'] for x in rows)}
metadata = {'source_sha256':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
            'hardware':False, 'toolchain':'ARMCC5 5.06 update 7 build 960; Cortex-M7.fp.sp; C99',
            'cases_per_optimization':125}
(HERE/'results.json').write_text(json.dumps({'summary':summary,'cases':rows,**metadata},indent=2),encoding='utf-8')
(HERE/'resources.json').write_text(json.dumps(resources,indent=2),encoding='utf-8')
print(summary)
raise SystemExit(bool(summary['fail']))
