"""Link the real HAL and execute both public write gates without mapped MMIO."""
from pathlib import Path

harness = Path(__file__).resolve().with_name('run_hal_link.py')
source = harness.read_text(encoding='utf-8')
source = source.replace("'output/QspiFlashQ1/hal_link'", "'output/QspiFlashQ1/q13/hal_gate'")
source = source.replace('hal_link_test.c', 'q13_hal_gate.c').replace('hal_link_test.o', 'q13_hal_gate.o')
source = source.replace("'--keep=q13_hal_gate.o(i.Q1_HalTimeoutEntry)'",
    "'--keep=q13_hal_gate.o(i.Q1_HalTimeoutEntry)', '--keep=q13_hal_gate.o(i.Q13_HalGateEntry)'")
source = source.replace("'HAL_QSPI_Abort',", "'HAL_QSPI_Abort', 'HAL_QSPI_Transmit', 'BSP_QSPI_Program', 'BSP_QSPI_EraseSector',")
source = source.replace("assert row['pass_']", """assert row['pass_']
# 新建 CPU，故意不映射任何外设；触碰寄存器会立即使仿真失败。
u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
for addr, size in [(0x08000000, 0x100000), (0x20000000, 0x100000), (0x01000000, 0x1000)]:
    u.mem_map(addr, size)
for addr, data in sections:
    u.mem_write(addr, data)
u.reg_write(UC_ARM_REG_SP, 0x200ffff0)
u.reg_write(UC_ARM_REG_LR, 0x01000001)
u.emu_start(symbols['Q13_HalGateEntry'] | 1, 0x01000000, count=1000000)
gate = dict(program=val('g_q13_program_result') - (1 << 32),
            erase=val('g_q13_erase_result') - (1 << 32), tick_calls=val('g_q13_gate_tick_calls'),
            peripherals_mapped=False, pass_=u.reg_read(UC_ARM_REG_R0) == 0 and
            u.reg_read(UC_ARM_REG_PC) == 0x01000000)
print(gate)
(OUT / 'results.json').write_text(json.dumps(dict(init_timeout=row, closed_gate=gate), indent=2))
assert gate['pass_']
""")
exec(compile(source, str(harness), 'exec'))
