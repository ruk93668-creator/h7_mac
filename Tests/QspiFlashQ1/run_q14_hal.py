"""Link actual ST HAL and simulate a stuck MCU controller through Recover."""
from pathlib import Path

harness = Path(__file__).resolve().with_name('run_hal_link.py')
source = harness.read_text(encoding='utf-8').replace("'output/QspiFlashQ1/hal_link'", "'output/QspiFlashQ1/q14/hal'")
source = source.replace('hal_link_test.c', 'q14_hal_recovery.c').replace('hal_link_test.o', 'q14_hal_recovery.o')
source = source.replace("'--keep=q14_hal_recovery.o(i.Q1_HalTimeoutEntry)'",
    "'--keep=q14_hal_recovery.o(i.Q1_HalTimeoutEntry)', '--keep=q14_hal_recovery.o(i.Q14_HalRecoveryEntry)'")
source = source.replace("'HAL_QSPI_Abort',", "'HAL_QSPI_Abort', 'BSP_QSPI_Recover', 'BSP_QSPI_GetFaultDiagnostics',")
source = source.replace("assert row['pass_']", """assert row['pass_']
u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
for addr,size in [(0x08000000,0x100000),(0x20000000,0x100000),(0x01000000,0x1000),
                  (0x40000000,0x40000),(0x52000000,0x10000),(0x58000000,0x40000),(0xE000E000,0x2000)]:
    u.mem_map(addr,size)
for addr,data in sections:
    u.mem_write(addr,data)
u.reg_write(UC_ARM_REG_SP,0x200ffff0)
u.reg_write(UC_ARM_REG_LR,0x01000001)
u.emu_start(symbols['Q14_HalRecoveryEntry'] | 1,0x01000000,count=2000000)
recovery = {name:val('g_q14_'+name) for name in
            ('init_result','recover_result','origin','latched','commands_absent','attempts')}
for key in ('init_result','recover_result'):
    recovery[key]=struct.unpack('<i',struct.pack('<I',recovery[key]))[0]
recovery['pass_']=u.reg_read(UC_ARM_REG_R0)==0 and u.reg_read(UC_ARM_REG_PC)==0x01000000
print(recovery)
(OUT/'results.json').write_text(json.dumps(dict(init_timeout=row,recovery_stuck_controller=recovery),indent=2))
assert recovery['pass_']
""")
exec(compile(source,str(harness),'exec'))
