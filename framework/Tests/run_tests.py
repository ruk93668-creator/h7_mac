import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

here = Path(__file__).resolve().parent
package = here.parent
root = package.parent
sys.path.insert(0, str(root / 'Tests/OledAsyncAcceptance/python_deps'))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile

commands = []
rows = []
tool = Path('D:/32/ARM/ARMCC/Bin')
names = ['init_order', 'init_fail_fast', 'event_dispatch_budget', 'one_shot_restart',
         'tick_wrap', 'generation_and_stop', 'periodic', 'list']

def run(command):
    command = list(map(str, command))
    p = subprocess.run(command, cwd=root, capture_output=True)
    output = (p.stdout + p.stderr).decode('utf-8', errors='replace')
    commands.append(dict(command=subprocess.list2cmdline(command), returncode=p.returncode, output=output))
    (here / 'commands.json').write_text(json.dumps(commands, indent=2), encoding='utf-8')
    if p.returncode:
        raise RuntimeError(output)

run([tool / 'armcc.exe', '--vsn'])
for mode in ['manual', 'armcc5']:
    args = [tool / 'armcc.exe', '--cpu', 'Cortex-M7', '--c99', '-O0',
            '-I', package / 'Inc', '-I', package / 'Port']
    if mode == 'armcc5':
        args += ['-DFRAMEWORK_USE_ARMCC5_INITCALL']
    sources = [package / 'Src' / n for n in ['framework_event.c', 'framework_timer.c', 'framework_runtime.c']]
    sources += [package / ('Src' if mode == 'manual' else 'Ports/ARMCC5') / 'framework_init.c',
                package / 'Port/platform_time.c', here / 'framework_test.c']
    objects = []
    for i, source in enumerate(sources):
        obj = here / f'{mode}_{i}.o'
        run(args + ['-c', source, '-o', obj])
        objects.append(obj)
    # 默认空表和main示例单独编译，避免与测试入口重复定义。
    for source in [package / 'Port/framework_init_table.c', package / 'Examples/main_example.c']:
        run(args + ['-c', source, '-o', here / f'{mode}_{source.stem}_compile.o'])
    image = here / f'{mode}.axf'
    scatter = here / ('armcc5.sct' if mode == 'armcc5' else 'test.sct')
    link = [tool / 'armlink.exe', *objects, '--entry', 'Framework_TestRun', '--scatter', scatter,
            '--no_scanlib', '-o', image]
    if mode == 'armcc5':
        link += ['--keep=*(.framework_init)']
    run(link)
    with image.open('rb') as f:
        elf = ELFFile(f)
        entry = elf.header['e_entry']
        symbols = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
        sections = [(s['sh_addr'], s.data()) for s in elf.iter_sections()
                    if s['sh_flags'] & 2 and s['sh_type'] == 'SHT_PROGBITS' and s['sh_size']]
    for case, name in enumerate(names):
        u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        for address, size in [(0x08000000, 0x100000), (0x20000000, 0x40000), (0x01000000, 0x1000)]:
            u.mem_map(address, size)
        for address, data in sections:
            u.mem_write(address, data)
        u.reg_write(UC_ARM_REG_SP, 0x2003FFF0)
        u.reg_write(UC_ARM_REG_LR, 0x01000001)
        u.reg_write(UC_ARM_REG_R0, case)
        error = None
        try:
            u.emu_start(entry | 1, 0x01000000, count=10000000, timeout=10000000)
        except Exception as exc:
            error = str(exc)
        result = u.reg_read(UC_ARM_REG_R0)
        checks = struct.unpack('<I', u.mem_read(symbols['g_checks'], 4))[0]
        passed = not error and result == 0 and u.reg_read(UC_ARM_REG_PC) == 0x01000000
        rows.append(dict(mode=mode, name=name, passed=passed, fail_line=result, checks=checks, error=error))
        print(mode, name, 'PASS' if passed else 'FAIL', result, flush=True)

manifest = json.loads((package / 'source_manifest.json').read_text())
for target, item in manifest.items():
    assert hashlib.sha256((package / target).read_bytes()).hexdigest() == item['sha256'], target
summary = dict(total=len(rows), passed=sum(r['passed'] for r in rows), failed=sum(not r['passed'] for r in rows))
(here / 'results.json').write_text(json.dumps(dict(summary=summary, cases=rows, exact_copy_hashes='PASS'), indent=2), encoding='utf-8')
print(summary)
sys.exit(bool(summary['failed']))
