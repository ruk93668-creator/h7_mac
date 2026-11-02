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
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_PC, UC_ARM_REG_R1, UC_ARM_REG_IPSR
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
for source in [HERE / "contract_test.c", ROOT / "CM7/Service/usb_uvc_desc.c", ROOT / "CM7/Service/usb_uvc_payload.c"] + list((ROOT / "Middlewares/ST/STM32_USB_Device_Library/Core/Src").glob("*.c")):
    obj = HERE / (source.stem + ".o")
    run(args + ["-c", source, "-o", obj])
    objects.append(obj)
# Exercise the unmodified ST IRQ and LL code under endpoint-local W1C register semantics.
import re
pcd = ROOT / 'Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c'
exports = re.findall(r'(?m)^(?:__weak\s+)?[\w *]+\b(HAL_PCD_\w+)\([^;{}]*\)\s*\{',pcd.read_text())
renames = ['-D'+name+'=Stream_'+name for name in sorted(set(exports)) if not name.endswith('Callback') and name not in ['HAL_PCD_MspInit','HAL_PCD_MspDeInit']]
obj=HERE/'real_pcd.o';run(args+renames+['-c',pcd,'-o',obj]);objects.append(obj)
obj=HERE/'real_ll.o';run(args+['-c',ROOT/'Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c','-o',obj]);objects.append(obj)
run(["D:/32/ARM/ARMCC/Bin/armlink.exe", *objects, "--entry", "UvcContract_TestRun", "--scatter",
     ROOT / "Tests/UsbDeviceStage1/test.sct", "--keep=*(.framework_init)", "--no_scanlib",
     "-o", HERE / "contract_test.axf"])
with (HERE / "contract_test.axf").open("rb") as f:
    elf = ELFFile(f)
    entry = elf.header["e_entry"]
    symbols = {s.name: s["st_value"] for s in elf.get_section_by_name(".symtab").iter_symbols()}
    sections = [(s["sh_addr"], s.data()) for s in elf.iter_sections()
                if s["sh_flags"] & 2 and s["sh_type"] == "SHT_PROGBITS" and s["sh_size"]]

names = ['初始Probe完整查询不授权', '初始Commit短读1B', '初始Commit完整查询', '初始Probe短读', '初始Probe超长上限截断', 'Windows先查询默认再协商开流', '初始默认查询被其他SETUP打断', '失败Probe返回默认但不授权Commit', '取消Abort失败保留帧', 'Incomplete错误不能伪装完成', 'IRQ调用契约', 'Class释放后查询输出不更新', 'Reset失败保留可查询所有权直到Stop成功', 'DeInit失败保留可查询所有权直到Stop成功']
rows = []
for case_id, name in enumerate(names):
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    for address, size in [(0x08000000,0x100000),(0x20000000,0x40000),(0x24000000,0x80000),(0x30000000,0x40000),(0x38000000,0x10000),(0x01000000,0x1000),(0x58020000,0x10000),(0xE000E000,0x2000),(0x40080000,0x40000)]:
        uc.mem_map(address,size)
    for address,data in sections: uc.mem_write(address,data)
    uc.reg_write(UC_ARM_REG_SP,0x2003FFF0); uc.reg_write(UC_ARM_REG_LR,0x01000001); uc.reg_write(UC_ARM_REG_R0,case_id)
    tx_trace = {"fixture_ep1_tx": 0, "class_ep1_tx": 0, "irq_submission": 0, "duplicate_sof": 0, "class_sof_calls": 0}
    submitted_sofs = set()
    def observe_tx(cpu, address, size, data):
        ep = cpu.reg_read(UC_ARM_REG_R1)
        if ep == 0x81:
            injecting = struct.unpack('<I', cpu.mem_read(symbols['g_fixture_tx'], 4))[0]
            tx_trace['fixture_ep1_tx' if injecting else 'class_ep1_tx'] += 1
            if not injecting:
                tick = struct.unpack('<I',cpu.mem_read(symbols['s_sof_sequence'],4))[0]
                if tick in submitted_sofs: tx_trace['duplicate_sof'] += 1
                submitted_sofs.add(tick)
                if struct.unpack('<I',cpu.mem_read(symbols['g_in_callback'],4))[0]: tx_trace['irq_submission'] += 1
    tx_addr = symbols['HAL_PCD_EP_Transmit'] & ~1
    uc.hook_add(UC_HOOK_CODE, observe_tx, begin=tx_addr, end=tx_addr)
    from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
    pending_w1c = {}
    w1c_addresses = [0x40080908 + 0x20*i for i in range(9)] + [0x40080014]
    def reg_write(cpu, access, address, size, value, user):
        if address not in w1c_addresses: return
        if struct.unpack('<I',cpu.mem_read(symbols['g_hw_raise'],4))[0]:
            pending_w1c.pop(address,None); return
        old = pending_w1c.get(address,struct.unpack('<I',cpu.mem_read(address,4))[0])
        pending_w1c[address] = old & ~value
    def reg_read(cpu, access, address, size, value, user):
        if address in pending_w1c:
            cpu.mem_write(address,struct.pack('<I',pending_w1c.pop(address)))
    uc.hook_add(UC_HOOK_MEM_WRITE,reg_write,begin=0x40080000,end=0x400bffff)
    uc.hook_add(UC_HOOK_MEM_READ,reg_read,begin=0x40080000,end=0x400bffff)
    def observe_sof(cpu, address, size, user): tx_trace['class_sof_calls'] += 1
    addr = symbols['UVC_Sof'] & ~1
    uc.hook_add(UC_HOOK_CODE,observe_sof,begin=addr,end=addr)
    if case_id == 10: uc.reg_write(UC_ARM_REG_IPSR, 11)
    error = None
    try: uc.emu_start(entry | 1,0x01000000,count=80000000,timeout=30000000)
    except Exception as exc: error = str(exc)
    values = struct.unpack("<6I",uc.mem_read(symbols["g_result"],24))
    passed = not error and uc.reg_read(UC_ARM_REG_PC)==0x01000000 and uc.reg_read(UC_ARM_REG_R0)==0 and values[0]==0 and tx_trace["irq_submission"]==0 and tx_trace["duplicate_sof"]==0 and (case_id!=31 or tx_trace["class_sof_calls"]==1)
    count = struct.unpack('<I',uc.mem_read(symbols['g_packet_count'],4))[0]
    packet_trace = {}
    for key, symbol in [('lengths','g_lengths'),('headers','g_headers'),('sofs','g_sofs')]:
        packet_trace[key] = list(struct.unpack('<%dI' % count,uc.mem_read(symbols[symbol],count*4))) if count else []
    rows.append({"id":case_id,"name":name,"pass":passed,"assertions":values[1],"failed_line":values[0],"actual":values[2],"expected":values[3],"error":error,"ep1_transmit_trace":tx_trace,"packet_trace":packet_trace})
    print(name,"PASS" if passed else "FAIL",values[:4],error)
changed = [p for p,h in before.items() if hashlib.sha256((ROOT/p).read_bytes()).hexdigest()!=h]
summary = {"total":len(rows),"passed":sum(r["pass"] for r in rows),"failed":sum(not r["pass"] for r in rows)}
(HERE/"results.json").write_text(json.dumps({"summary":summary,"source_changed":changed,"class_context_size":struct.unpack("<I",uc.mem_read(symbols["g_uvc_context_size"],4))[0],"cases":rows},ensure_ascii=False,indent=2),encoding="utf-8")
print(summary,"source_changed",changed)
raise SystemExit(bool(summary["failed"] or changed))
