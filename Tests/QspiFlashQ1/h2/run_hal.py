"""Execute real ST command and receive through the H2 adapter with synthetic MMIO."""
from pathlib import Path

harness = Path(__file__).resolve().parents[1] / 'run_hal_link.py'
source = harness.read_text(encoding='utf-8')
source = source.replace("'output/QspiFlashQ1/hal_link'", "'output/QspiFlashQ1/h2/hal'")
source = source.replace("'Tests/QspiFlashQ1/hal_link_test.c', 'CM7/BSP/bsp_qspi.c',",
                        "'Tests/QspiFlashQ1/h2/h2_hal_test.c', 'Tests/QspiFlashQ1/h2/h2.c', "
                        "'Tests/QspiFlashQ1/h2/bsp_qspi_h2.c',")
source = source.replace("'--entry', 'Q1_LinkEntry',", "'--entry', 'H2_HalEntry',")
source = source.replace("'--keep=hal_link_test.o(i.Q1_HalTimeoutEntry)', ", "")
source = source[:source.index('assert all(x in symbols')] + """
assert all(name in symbols for name in ('H2_HalEntry', 'H2_Command', 'H2_Receive',
                                         'HAL_QSPI_Command', 'HAL_QSPI_Receive'))
u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
for addr, size in [(0x08000000, 0x100000), (0x20000000, 0x100000),
                   (0x52000000, 0x10000), (0x01000000, 0x1000)]:
    u.mem_map(addr, size)
for addr, data in sections:
    u.mem_write(addr, data)
u.reg_write(UC_ARM_REG_SP, 0x200ffff0)
u.reg_write(UC_ARM_REG_LR, 0x01000001)
u.emu_start(symbols['H2_HalEntry'] | 1, 0x01000000, count=1000000)
def value(name):
    return struct.unpack('<I', u.mem_read(symbols['g_h2_hal_' + name], 4))[0]
row = dict(assertions=value('assertions'), failure_line=value('failure'),
           returned=u.reg_read(UC_ARM_REG_PC) == 0x01000000)
row['pass'] = row['returned'] and row['failure_line'] == 0 and u.reg_read(UC_ARM_REG_R0) == 0
(OUT / 'results.json').write_text(json.dumps(row, indent=2) + '\\n')
print(row)
assert row['pass']
"""
exec(compile(source, str(harness), 'exec'), {'__file__': str(harness), '__name__': '__main__'})
