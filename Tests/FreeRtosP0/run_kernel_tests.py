"""Real FreeRTOS kernel and RVDS port; scheduler start exception is simulated explicitly."""
from pathlib import Path
import hashlib
import json
import struct
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8")
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "Tests/OledAsyncAcceptance/python_deps"))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile

files = ["CM7/Platform/platform_rtos.c", "CM7/Platform/platform_hal_timebase.c", "CM7/Platform/FreeRTOSConfig.h"]
before = {f: hashlib.sha256((ROOT / f).read_bytes()).hexdigest() for f in files}
commands = []

def run(args):
    result = subprocess.run(list(map(str, args)), cwd=ROOT, capture_output=True)
    output = (result.stdout + result.stderr).decode("utf-8", errors="replace")
    commands.append({"command": subprocess.list2cmdline(list(map(str, args))),
                     "returncode": result.returncode, "output": output})
    (HERE / "commands.json").write_text(json.dumps(commands, ensure_ascii=False, indent=2), encoding="utf-8")
    if result.returncode:
        raise RuntimeError(output)

run(["D:/32/ARM/ARMCC/Bin/armcc.exe", "--vsn"])
args = ["D:/32/ARM/ARMCC/Bin/armcc.exe", "--cpu", "Cortex-M7.fp.sp", "--c99", "-O0",
        "-DSTM32H747xx", "-DCORE_CM7", "-DUSE_HAL_DRIVER"]
for inc in ["CM7/Core/Inc", "CM7/Platform", "Common/Inc", "Drivers/STM32H7xx_HAL_Driver/Inc", "Drivers/CMSIS/Include", "Drivers/CMSIS/Device/ST/STM32H7xx/Include", "Middlewares/Third_Party/FreeRTOS/Source/include", "Middlewares/Third_Party/FreeRTOS/Source/portable/RVDS/ARM_CM7/r0p1"]:
    args += ["-I", ROOT / inc]
objects = []
for source in [HERE / "kernel_test.c", ROOT / "Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_cortex.c"] + [ROOT / "Middlewares/Third_Party/FreeRTOS/Source" / name for name in ["tasks.c", "list.c", "queue.c", "portable/RVDS/ARM_CM7/r0p1/port.c"]]:
    obj = HERE / (source.stem + ".o")
    run(args + ["-c", source, "-o", obj])
    objects.append(obj)
run(["D:/32/ARM/ARMCC/Bin/armlink.exe", *objects, "--entry", "Kernel_TestRun", "--scatter",
     ROOT / "Tests/UsbDeviceStage1/test.sct", "--no_scanlib",
     "-o", HERE / "kernel_test.axf"])
with (HERE / "kernel_test.axf").open("rb") as f:
    elf = ELFFile(f)
    entry = elf.header["e_entry"]
    symbols = {s.name: s["st_value"] for s in elf.get_section_by_name(".symtab").iter_symbols()}
    sections = [(s["sh_addr"], s.data()) for s in elf.iter_sections()
                if s["sh_flags"] & 2 and s["sh_type"] == "SHT_PROGBITS" and s["sh_size"]]

names = ['null callback', 'masked context rejected', 'static startup and duplicate rejection', 'static idle storage', 'priority delay wake and idle', 'suspended tick catchup', 'stack watermark alignment and resume', 'real Port SysTick and independent HAL', 'APB1 TIMPRE clock derivation', 'TIM6 reinit suspend resume priority', 'stack overflow hook']
rows = []
for case_id, name in enumerate(names):
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    for address, size in [(0x08000000,0x100000),(0x20000000,0x40000),(0x24000000,0x80000),(0x30000000,0x40000),(0x38000000,0x10000),(0x01000000,0x1000),(0x58020000,0x10000),(0xE000E000,0x2000),(0x40000000,0x100000)]:
        uc.mem_map(address,size)
    for address,data in sections: uc.mem_write(address,data)
    uc.reg_write(UC_ARM_REG_SP,0x2003FFF0); uc.reg_write(UC_ARM_REG_LR,0x01000001); uc.reg_write(UC_ARM_REG_R0,case_id)
    from unicorn import UC_HOOK_CODE
    from unicorn.arm_const import UC_ARM_REG_BASEPRI
    uc.mem_write(0xE000ED0C, struct.pack('<I', 3 << 8))
    overflow_seen = []
    def code_hook(cpu, address, size, _):
        if address == (symbols['xPortStartScheduler'] & ~1):
            # No simulated NVIC exception entry/return: test kernel scheduling separately.
            cpu.mem_write(symbols['uxCriticalNesting'], struct.pack('<I',0))
            cpu.reg_write(UC_ARM_REG_BASEPRI,0)
            cpu.reg_write(UC_ARM_REG_R0,1)
            cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))
        if address == (symbols['Platform_RtosAssert'] & ~1):
            overflow_seen.append(True)
            cpu.emu_stop()
    uc.hook_add(UC_HOOK_CODE, code_hook)
    from unicorn import UC_HOOK_MEM_INVALID
    def invalid(cpu, access, address, size, value, _):
        print('INVALID',hex(address),'PC',hex(cpu.reg_read(UC_ARM_REG_PC)),'LR',hex(cpu.reg_read(UC_ARM_REG_LR)))
        return False
    uc.hook_add(UC_HOOK_MEM_INVALID, invalid)
    error = None
    try: uc.emu_start(entry | 1,0x01000000,count=10000000,timeout=10000000)
    except Exception as exc: error = str(exc)
    values = struct.unpack("<4I",uc.mem_read(symbols["g_result"],16))
    passed = not error and uc.reg_read(UC_ARM_REG_PC)==0x01000000 and uc.reg_read(UC_ARM_REG_R0)==0 and values[0]==0
    if case_id == 10:
        passed = not error and bool(overflow_seen) and values[0] == 0
    else:
        passed = passed and not overflow_seen
    rows.append({"id":case_id,"name":name,"pass":passed,"assertions":values[1],"failed_line":values[0],"actual":values[2],"expected":values[3],"error":error})
    print(name,"PASS" if passed else "FAIL",values[:4],error)
changed = [p for p,h in before.items() if hashlib.sha256((ROOT/p).read_bytes()).hexdigest()!=h]
summary = {"total":len(rows),"passed":sum(r["pass"] for r in rows),"failed":sum(not r["pass"] for r in rows)}
(HERE/"results.json").write_text(json.dumps({"summary":summary,"source_changed":changed,"cases":rows},ensure_ascii=False,indent=2),encoding="utf-8")
print(summary,"source_changed",changed)
raise SystemExit(bool(summary["failed"] or changed))
