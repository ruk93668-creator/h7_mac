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
sys.path.insert(0, "D:/32/STM32H7/h7_mac/Tests/OledAsyncAcceptance/python_deps")
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_PC, UC_ARM_REG_R1
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
for source in [HERE / "ep1_test.c", ROOT / "CM7/Service/usb_uvc_desc.c", ROOT / "CM7/Service/usb_uvc_payload.c"] + list((ROOT / "Middlewares/ST/STM32_USB_Device_Library/Core/Src").glob("*.c")):
    obj = HERE / (source.stem + ".o")
    run(args + ["-c", source, "-o", obj])
    objects.append(obj)
run(["D:/32/ARM/ARMCC/Bin/armlink.exe", *objects, "--entry", "UvcEp1_TestRun", "--scatter",
     ROOT / "Tests/UsbDeviceStage1/test.sct", "--keep=*(.framework_init)", "--no_scanlib",
     "-o", HERE / "ep1_test.axf"])
with (HERE / "ep1_test.axf").open("rb") as f:
    elf = ELFFile(f)
    entry = elf.header["e_entry"]
    symbols = {s.name: s["st_value"] for s in elf.get_section_by_name(".symtab").iter_symbols()}
    sections = [(s["sh_addr"], s.data()) for s in elf.iter_sections()
                if s["sh_flags"] & 2 and s["sh_type"] == "SHT_PROGBITS" and s["sh_size"]]

names = ['未Commit拒绝Alt1', 'Alt0到Alt1到Alt0', '打开参数严格匹配编译后描述符', '重复Alt1不重复Open', '重复Alt0不重复Close', 'VC Alt0不影响VS Alt1', 'VC拒绝Alt1', 'VS拒绝Alt2', '拒绝Alt高字节', '拒绝接口2', '拒绝接口高字节', '拒绝SET方向错误', '拒绝SET非零长度', 'Open HAL_ERROR后重试', 'Open HAL_BUSY后重试', '空闲Close失败保留状态并重试', 'busy Abort失败保留PIO及重试', 'busy Flush失败保留PIO及重试', 'busy Close失败只归还已取消传输', 'DeInit关闭Alt1并归还上下文', 'DeInit Abort失败保留上下文', 'DeInit Close失败保留上下文', 'Reset回收并要求重新Commit', 'Reset Abort失败和Stop恢复', 'Reset Close失败和Stop恢复', '重复Alt1保留busy缓冲区', '新Probe独立于当前Commit', 'Alt0后保留Commit可重新Alt1', 'GET_INTERFACE反映实际Alt', 'Open失败STALL后GET_DESCRIPTOR恢复', 'Close失败STALL后GET_STATUS恢复', '失败后强制Status仍被Platform拒绝', '重复DeInit不重复释放', 'SET_CONFIGURATION0正常释放', 'SET_CONFIGURATION0 Abort失败不得成功Status', 'SET_CONFIGURATION0 Close失败不得成功Status', 'Device Stop后可重新配置', 'Device Stop失败不释放busy资源', 'DataIn完成不自动发送下一包', 'Disconnect正常回收', 'Alt0 DeInit不操作EP1', '关闭失败后Alt1必须拒绝直到Alt0恢复', 'Alt1时非法SET保持端点状态']
names += ["失败新Probe不清除活动Commit", "失败新Commit保留原活动Commit"]
rows = []
for case_id, name in enumerate(names):
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    for address, size in [(0x08000000,0x100000),(0x20000000,0x40000),(0x24000000,0x80000),(0x30000000,0x40000),(0x38000000,0x10000),(0x01000000,0x1000),(0x58020000,0x10000),(0xE000E000,0x2000),(0x40080000,0x40000)]:
        uc.mem_map(address,size)
    for address,data in sections: uc.mem_write(address,data)
    uc.reg_write(UC_ARM_REG_SP,0x2003FFF0); uc.reg_write(UC_ARM_REG_LR,0x01000001); uc.reg_write(UC_ARM_REG_R0,case_id)
    tx_trace = {"fixture_ep1_tx": 0, "class_ep1_tx": 0}
    def observe_tx(cpu, address, size, data):
        ep = cpu.reg_read(UC_ARM_REG_R1)
        if ep == 0x81:
            injecting = struct.unpack('<I', cpu.mem_read(symbols['g_fixture_tx'], 4))[0]
            tx_trace['fixture_ep1_tx' if injecting else 'class_ep1_tx'] += 1
    tx_addr = symbols['HAL_PCD_EP_Transmit'] & ~1
    uc.hook_add(UC_HOOK_CODE, observe_tx, begin=tx_addr, end=tx_addr)
    error = None
    try: uc.emu_start(entry | 1,0x01000000,count=10000000,timeout=10000000)
    except Exception as exc: error = str(exc)
    values = struct.unpack("<6I",uc.mem_read(symbols["g_result"],24))
    passed = not error and uc.reg_read(UC_ARM_REG_PC)==0x01000000 and uc.reg_read(UC_ARM_REG_R0)==0 and values[0]==0 and tx_trace["class_ep1_tx"]==0
    rows.append({"id":case_id,"name":name,"pass":passed,"assertions":values[1],"failed_line":values[0],"actual":values[2],"expected":values[3],"error":error,"ep1_transmit_trace":tx_trace})
    print(name,"PASS" if passed else "FAIL",values[:4],error)
changed = [p for p,h in before.items() if hashlib.sha256((ROOT/p).read_bytes()).hexdigest()!=h]
summary = {"total":len(rows),"passed":sum(r["pass"] for r in rows),"failed":sum(not r["pass"] for r in rows)}
(HERE/"results.json").write_text(json.dumps({"summary":summary,"source_changed":changed,"cases":rows},ensure_ascii=False,indent=2),encoding="utf-8")
print(summary,"source_changed",changed)
raise SystemExit(bool(summary["failed"] or changed))
