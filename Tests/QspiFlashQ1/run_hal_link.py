"""Link BSP with real ST QSPI/GPIO; execute real HAL Init timeout in Unicorn."""
from pathlib import Path
import subprocess, sys, json, struct

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'output/QspiFlashQ1/hal_link'
OUT.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(ROOT / 'output/QspiFlashQ1/python_deps'))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0
from elftools.elf.elffile import ELFFile

commands = []
def run(cmd):
    p = subprocess.run(list(map(str, cmd)), cwd=ROOT, capture_output=True)
    txt = (p.stdout + p.stderr).decode('utf-8', errors='replace')
    commands.append(dict(command=list(map(str, cmd)), exit=p.returncode, output=txt))
    (OUT / 'commands.json').write_text(json.dumps(commands, indent=2))
    if p.returncode:
        raise RuntimeError(txt)
    return txt

cc = ['D:/32/ARM/ARMCC/Bin/armcc.exe', '--cpu', 'Cortex-M7.fp.sp', '--c99', '-O2',
      '-DSTM32H747xx', '-DCORE_CM7', '-DUSE_HAL_DRIVER', '--split_sections']
for inc in ['CM7/Core/Inc', 'CM7/BSP', 'Common/Inc', 'Drivers/STM32H7xx_HAL_Driver/Inc',
            'Drivers/CMSIS/Include', 'Drivers/CMSIS/Device/ST/STM32H7xx/Include']:
    cc += ['-I', ROOT / inc]
objects = []
for rel in ['Tests/QspiFlashQ1/hal_link_test.c', 'CM7/BSP/bsp_qspi.c',
            'Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_qspi.c',
            'Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_mdma.c',
            'Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_gpio.c']:
    obj = OUT / (Path(rel).stem + '.o')
    run(cc + ['-c', ROOT / rel, '-o', obj])
    objects.append(obj)
sc = OUT / 'link.sct'
sc.write_text('LR_TEST 0x08000000 0x100000 { ER_TEST 0x08000000 { .ANY (+RO) } RW_TEST 0x20000000 0x100000 { .ANY (+RW +ZI) } }\n')
axf = OUT / 'real_hal.axf'
text = run(['D:/32/ARM/ARMCC/Bin/armlink.exe', *objects, '--entry', 'Q1_LinkEntry',
            '--keep=hal_link_test.o(i.Q1_HalTimeoutEntry)', '--scatter', sc,
            '--map', '--info=sizes', '-o', axf])
(OUT / 'link_map.log').write_text(text)
with axf.open('rb') as f:
    elf = ELFFile(f)
    symbols = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
    sections = [(s['sh_addr'], s.data()) for s in elf.iter_sections()
                if s['sh_flags'] & 2 and s['sh_type'] == 'SHT_PROGBITS' and s['sh_size']]
assert all(x in symbols for x in ['BSP_QSPI_Init', 'HAL_QSPI_Init', 'HAL_QSPI_Command',
                                'HAL_QSPI_Receive', 'HAL_QSPI_Abort', 'HAL_GPIO_Init'])
u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
for addr, size in [(0x08000000, 0x100000), (0x20000000, 0x100000),
                   (0x52000000, 0x10000), (0x01000000, 0x1000)]:
    u.mem_map(addr, size)
for addr, data in sections:
    u.mem_write(addr, data)
u.reg_write(UC_ARM_REG_SP, 0x200ffff0)
u.reg_write(UC_ARM_REG_LR, 0x01000001)
u.emu_start(symbols['Q1_HalTimeoutEntry'] | 1, 0x01000000, count=1000000)
def val(name):
    return struct.unpack('<I', u.mem_read(symbols[name], 4))[0]
row = dict(real_hal_timeout=val('g_timeout_result'), timeout_value=val('g_timeout_value'),
           error_bits=val('g_timeout_error'),
           elapsed_tick_calls=val('g_timeout_elapsed'),
           pass_=u.reg_read(UC_ARM_REG_R0) == 0 and u.reg_read(UC_ARM_REG_PC) == 0x01000000)
print(row)
(OUT / 'results.json').write_text(json.dumps(row, indent=2))
assert row['pass_']
