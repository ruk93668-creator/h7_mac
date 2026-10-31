"""ARMCC5 + Unicorn: real Mode/BSP/Platform/Core, mocked HAL hardware."""
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

files = ["CM7/Service/usb_mode.c", "CM7/Service/usb_mode.h",
         "CM7/Platform/usb_device.c", "CM7/BSP/bsp_usb.c", "CM7/Service/usb_uvc_class.c",
         "CM7/Platform/usb_device.h", "CM7/BSP/bsp_usb.h", "CM7/Service/usb_test_device.c"]
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
args = ["D:/32/ARM/ARMCC/Bin/armcc.exe", "--cpu", "Cortex-M7", "--c99", "-O0",
        "-DSTM32H747xx", "-DCORE_CM7", "-DUSE_HAL_DRIVER", "--preinclude",
        ROOT / "CM7/BSP/bsp_usb_hal_config.h"]
for inc in ["CM7/Core/Inc", "CM7/BSP", "CM7/Platform", "CM7/Service", "CM7/Framework", "Common/Inc",
            "Drivers/STM32H7xx_HAL_Driver/Inc", "Drivers/CMSIS/Include",
            "Drivers/CMSIS/Device/ST/STM32H7xx/Include",
            "Middlewares/ST/STM32_USB_Device_Library/Core/Inc"]:
    args += ["-I", ROOT / inc]
objects = []
for source in [HERE / "lifecycle_test.c"] + list((ROOT / "Middlewares/ST/STM32_USB_Device_Library/Core/Src").glob("*.c")):
    obj = HERE / (source.stem + ".o")
    run(args + ["-c", source, "-o", obj])
    objects.append(obj)
run(["D:/32/ARM/ARMCC/Bin/armlink.exe", *objects, "--entry", "UvcLifecycle_TestRun", "--scatter",
     ROOT / "Tests/UsbDeviceStage1/test.sct", "--keep=*(.framework_init)", "--no_scanlib",
     "-o", HERE / "mode_test.axf"])
with (HERE / "mode_test.axf").open("rb") as f:
    elf = ELFFile(f)
    entry = elf.header["e_entry"]
    symbols = {s.name: s["st_value"] for s in elf.get_section_by_name(".symtab").iter_symbols()}
    sections = [(s["sh_addr"], s.data()) for s in elf.iter_sections()
                if s["sh_flags"] & 2 and s["sh_type"] == "SHT_PROGBITS" and s["sh_size"]]

names = ["Init空句柄", "Init配置0", "Init配置2", "重复Init保持原上下文", "固定池耗尽", "完整清零与Alt0", "DeInit空句柄", "空上下文DeInit", "正常及重复DeInit", "Core延迟归还", "释放后可重新Init"]
rows = []
for case_id, name in enumerate(names):
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    for address, size in [(0x08000000,0x100000),(0x20000000,0x40000),(0x24000000,0x80000),(0x30000000,0x40000),(0x38000000,0x10000),(0x01000000,0x1000),(0x58020000,0x10000),(0xE000E000,0x2000),(0x40080000,0x40000)]:
        uc.mem_map(address,size)
    for address,data in sections: uc.mem_write(address,data)
    uc.reg_write(UC_ARM_REG_SP,0x2003FFF0); uc.reg_write(UC_ARM_REG_LR,0x01000001); uc.reg_write(UC_ARM_REG_R0,case_id)
    error = None
    try: uc.emu_start(entry | 1,0x01000000,count=10000000,timeout=10000000)
    except Exception as exc: error = str(exc)
    values = struct.unpack("<6I",uc.mem_read(symbols["g_result"],24))
    passed = not error and uc.reg_read(UC_ARM_REG_PC)==0x01000000 and uc.reg_read(UC_ARM_REG_R0)==0 and values[0]==0
    rows.append({"id":case_id,"name":name,"pass":passed,"assertions":values[1],"failed_line":values[0],"actual":values[2],"expected":values[3],"error":error})
    print(name,"PASS" if passed else "FAIL",values[:4],error)
changed = [p for p,h in before.items() if hashlib.sha256((ROOT/p).read_bytes()).hexdigest()!=h]
summary = {"total":len(rows),"passed":sum(r["pass"] for r in rows),"failed":sum(not r["pass"] for r in rows)}
(HERE/"results.json").write_text(json.dumps({"summary":summary,"source_changed":changed,"cases":rows},ensure_ascii=False,indent=2),encoding="utf-8")
print(summary,"source_changed",changed)
raise SystemExit(bool(summary["failed"] or changed))
