"""ARMCC5 + Unicorn execute production BSP against HAL mocks, never a probe."""
from pathlib import Path
import subprocess, sys, struct, json, hashlib

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
OUT = ROOT / 'output/QspiFlashQ1/mock'
sys.path.insert(0, str(ROOT / 'output/QspiFlashQ1/python_deps'))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile

OUT.mkdir(parents=True, exist_ok=True)
commands = []
def run(cmd):
    p = subprocess.run(list(map(str, cmd)), cwd=ROOT, capture_output=True)
    text = (p.stdout + p.stderr).decode('utf-8', errors='replace')
    commands.append(dict(command=list(map(str, cmd)), exit=p.returncode, output=text))
    (OUT / 'commands.json').write_text(json.dumps(commands, indent=2))
    if p.returncode:
        raise RuntimeError(text)

cc = ['D:/32/ARM/ARMCC/Bin/armcc.exe', '--cpu', 'Cortex-M7.fp.sp', '--c99', '-O0',
      '-DSTM32H747xx', '-DCORE_CM7', '-DUSE_HAL_DRIVER']
for inc in ['CM7/Core/Inc', 'CM7/BSP', 'Common/Inc', 'Drivers/STM32H7xx_HAL_Driver/Inc',
            'Drivers/CMSIS/Include', 'Drivers/CMSIS/Device/ST/STM32H7xx/Include']:
    cc += ['-I', ROOT / inc]
run(cc + ['-c', HERE / 'behavior_test.c', '-o', OUT / 'behavior.o'])
sc = OUT / 'test.sct'
sc.write_text('LR_TEST 0x08000000 0x100000 { ER_TEST 0x08000000 { .ANY (+RO) } RW_TEST 0x20000000 0x100000 { .ANY (+RW +ZI) } }\n')
run(['D:/32/ARM/ARMCC/Bin/armlink.exe', OUT / 'behavior.o', '--entry', 'Q1_TestRun',
     '--scatter', sc, '-o', OUT / 'behavior.axf'])
with (OUT / 'behavior.axf').open('rb') as f:
    elf = ELFFile(f)
    syms = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
    sections = [(s['sh_addr'], s.data()) for s in elf.iter_sections()
                if s['sh_flags'] & 2 and s['sh_type'] == 'SHT_PROGBITS' and s['sh_size']]
names = ['normal', 'duplicate', 'deinit and reinit', 'GPIO prepare failure', 'MSP absent',
         'MSP failure HAL_OK', 'HAL init error', 'config mismatch', 'ID zero', 'ID FF',
         'ID wrong', 'command busy', 'command error', 'command timeout', 'receive busy',
         'receive error', 'receive timeout', 'PRIMASK', 'BASEPRI', 'FAULTMASK', 'TIM stopped',
         'TIM IRQ off', 'NVIC off', 'operation busy', 'HCLK zero', 'clock round up',
         'reentry', 'tick wrap', 'short receive', 'wrong length', 'shared deadline',
         'HAL default timeout', 'init over budget', 'cleanup failure', 'receive over budget',
         'post receive config changed', 'context lost', 'HAL_OK with error', 'foreign clock owner',
         'controller busy', 'ISR', 'occupied getters and deinit', 'cleanup deadline', 'deinit failure',
         'HAL_ERROR with timeout bit', 'RCC mux mismatch', 'RCC gate failure', 'GPIO speed mismatch',
         'BASEPRI allows HAL tick', 'unexpected DMA enable', 'DLR length corruption']
rows = []
for case, name in enumerate(names):
    u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    for addr, size in [(0x08000000, 0x100000), (0x20000000, 0x100000),
                       (0x40000000, 0x40000), (0x52000000, 0x10000),
                       (0x58000000, 0x40000), (0xE000E000, 0x2000), (0x01000000, 0x1000)]:
        u.mem_map(addr, size)
    for addr, data in sections:
        u.mem_write(addr, data)
    u.reg_write(UC_ARM_REG_SP, 0x200ffff0)
    u.reg_write(UC_ARM_REG_LR, 0x01000001)
    u.reg_write(UC_ARM_REG_R0, case)
    error = None
    try:
        u.emu_start(syms['Q1_TestRun'] | 1, 0x01000000, count=2000000)
    except Exception as ex:
        error = str(ex)
    def value(symbol):
        return struct.unpack('<I', u.mem_read(syms[symbol], 4))[0]
    row = dict(case=case, name=name, assertions=value('g_assertions'), failure_line=value('g_failure'),
               commands=value('g_commands'), hal_init_calls=value('g_init_calls'), error=error)
    row['pass'] = not error and row['failure_line'] == 0 and u.reg_read(UC_ARM_REG_PC) == 0x01000000 and u.reg_read(UC_ARM_REG_R0) == 0
    rows.append(row)
    print(json.dumps(row), flush=True)
summary = dict(cases=len(rows), assertions=sum(r['assertions'] for r in rows),
               passed=sum(r['pass'] for r in rows), rows=rows,
               source_sha256=hashlib.sha256((ROOT / 'CM7/BSP/bsp_qspi.c').read_bytes()).hexdigest())
(OUT / 'results.json').write_text(json.dumps(summary, indent=2))
(OUT / 'run.log').write_text('\n'.join(json.dumps(r) for r in rows) + '\n', encoding='utf-8')
sys.exit(not all(r['pass'] for r in rows))
