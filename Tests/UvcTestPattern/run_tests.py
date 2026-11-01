"""ARMCC5 + Unicorn; actual Application/Mode/Class/Payload/Core/BSP, mock hardware."""
from pathlib import Path
import hashlib
import json
import re
import struct
import subprocess
import sys
import zlib

sys.stdout.reconfigure(encoding="utf-8")
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, "D:/32/STM32H7/h7_mac/Tests/OledAsyncAcceptance/python_deps")
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile

NAMES = {
    0: "TEST默认枚举，200SOF不生成或提交视频",
    1: "TEST重复Init/Start不重复启动",
    2: "TEST拒绝运行时切换UVC",
    3: "TEST保留500ms非阻塞同模式重连",
    4: "TEST显式注册后Init不重复注册",
    5: "TEST遵守DEVICE/SERVICE初始化顺序",
    10: "UVC注册和枚举成功，无Commit不提交",
    11: "UVC有Commit但无Alt1不提交",
    12: "固定彩条两帧各76包，FID/EOF和100SOF节奏",
    13: "未枚举时不发送，5000ms非阻塞超时",
    14: "UVC重复Init/Start不重复注册或启动",
    15: "图像被借用时不重写、不重复提交",
    16: "Alt0停止提交，重新Alt1可以借出",
    17: "Reset归还旧帧，新Commit/Alt1后恢复",
    18: "Disconnect归还，重新枚举后恢复",
    19: "Incomplete/Abort失败保留帧，成功后恢复",
    20: "Alt0 Abort失败不虚假提交新帧",
    21: "Reset Abort失败保留Class和图像，Stop后释放",
    22: "单包完成不归还整帧，不改变Commit",
    23: "UVC拒绝热切换TEST/MSC",
    24: "UVC显式注册后Init不重复注册",
    25: "UVC遵守DEVICE/SERVICE初始化顺序",
    26: "UVC图像应用不依赖Camera初始化",
    27: "中途HAL_BUSY不提前归还图像",
}
commands = []
sources = [ROOT / p for p in ["CM7/Application/app_uvc_test.c", "CM7/Application/app_uvc_test.h",
    "CM7/Application/app_main.c", "CM7/Service/usb_mode.c", "CM7/Service/usb_mode.h",
    "CM7/Service/usb_uvc_class.c", "CM7/Service/usb_uvc.h"]]
before = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}


def run(args):
    result = subprocess.run(list(map(str, args)), cwd=ROOT, capture_output=True)
    output = (result.stdout + result.stderr).decode("utf-8", errors="replace")
    commands.append({"command": subprocess.list2cmdline(list(map(str, args))), "returncode": result.returncode, "output": output})
    (HERE / "commands.json").write_text(json.dumps(commands, ensure_ascii=False, indent=2), encoding="utf-8")
    if result.returncode:
        raise RuntimeError(output)


run(["D:/32/ARM/ARMCC/Bin/armcc.exe", "--vsn"])
rows = []
for mode in (0, 1):
    args = ["D:/32/ARM/ARMCC/Bin/armcc.exe", "--cpu", "Cortex-M7", "--c99", "-O0",
        "-DSTM32H747xx", "-DCORE_CM7", "-DUSE_HAL_DRIVER", f"-DUSB_MODE_UVC_TEST_ENABLED={mode}",
        "--preinclude", ROOT / "CM7/BSP/bsp_usb_hal_config.h"]
    for inc in ["CM7/Core/Inc", "CM7/BSP", "CM7/Platform", "CM7/Service", "CM7/Framework", "CM7/Application", "Common/Inc",
        "Drivers/STM32H7xx_HAL_Driver/Inc", "Drivers/CMSIS/Include", "Drivers/CMSIS/Device/ST/STM32H7xx/Include",
        "Middlewares/ST/STM32_USB_Device_Library/Core/Inc"]:
        args += ["-I", ROOT / inc]
    objects = []
    for source in [HERE / "pattern_test.c", ROOT / "CM7/Service/usb_uvc_desc.c", ROOT / "CM7/Service/usb_uvc_payload.c"] + list((ROOT / "Middlewares/ST/STM32_USB_Device_Library/Core/Src").glob("*.c")):
        obj = HERE / (source.stem + f"_{mode}.o")
        run(args + ["-c", source, "-o", obj]); objects.append(obj)
    pcd = ROOT / "Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c"
    exports = re.findall(r"(?m)^(?:__weak\s+)?[\w *]+\b(HAL_PCD_\w+)\([^;{}]*\)\s*\{", pcd.read_text())
    renames = ["-D"+n+"=Stream_"+n for n in sorted(set(exports)) if not n.endswith("Callback") and n not in ["HAL_PCD_MspInit", "HAL_PCD_MspDeInit"]]
    obj = HERE / f"real_pcd_{mode}.o"; run(args + renames + ["-c", pcd, "-o", obj]); objects.append(obj)
    obj = HERE / f"real_ll_{mode}.o"; run(args + ["-c", ROOT / "Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c", "-o", obj]); objects.append(obj)
    image = HERE / f"pattern_{mode}.axf"
    run(["D:/32/ARM/ARMCC/Bin/armlink.exe", *objects, "--entry", "UvcPattern_TestRun", "--scatter", ROOT / "Tests/UsbDeviceStage1/test.sct", "--keep=*(.framework_init)", "--no_scanlib", "-o", image])
    with image.open("rb") as f:
        elf = ELFFile(f); entry = elf.header["e_entry"]
        syms = {s.name: (s["st_value"], s["st_size"]) for s in elf.get_section_by_name(".symtab").iter_symbols()}
        sections = [(s["sh_addr"], s.data()) for s in elf.iter_sections() if s["sh_flags"] & 2 and s["sh_type"] == "SHT_PROGBITS" and s["sh_size"]]
    for case_id, name in NAMES.items():
        if (case_id < 10) != (mode == 0):
            continue
        uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        for address, size in [(0x08000000,0x100000),(0x20000000,0x40000),(0x24000000,0x80000),(0x30000000,0x40000),(0x38000000,0x10000),(0x01000000,0x1000),(0x58020000,0x10000),(0xE000E000,0x2000),(0x40080000,0x40000)]:
            uc.mem_map(address, size)
        for address, data in sections: uc.mem_write(address, data)
        uc.reg_write(UC_ARM_REG_SP, 0x2003FFF0); uc.reg_write(UC_ARM_REG_LR, 0x01000001); uc.reg_write(UC_ARM_REG_R0, case_id)
        counts = {"register_class":0, "generate":0, "submit_frame":0, "image_written_bytes":0, "writes_after_freeze":0, "generator_out_of_bounds":0}
        if mode == 0:
            assert all(n not in syms for n in ["s_test_frame", "s_color_bars", "UvcTest_GeneratePattern", "pattern_initialized", "s_pattern_ready", "pattern_last_result"])
        frame, frame_size = syms.get("s_test_frame", (0, 0))
        generate, generate_size = syms.get("UvcTest_GeneratePattern", (0, 0)); generate &= ~1

        def code(cpu, addr, size, user):
            for key, symbol in [("register_class", "USBD_RegisterClass"), ("generate", "UvcTest_GeneratePattern"), ("submit_frame", "USB_UVC_SubmitFrame")]:
                if symbol in syms and addr == (syms[symbol][0] & ~1): counts[key] += 1

        for symbol in ["USBD_RegisterClass", "UvcTest_GeneratePattern", "USB_UVC_SubmitFrame"]:
            if symbol not in syms: continue
            addr = syms[symbol][0] & ~1
            uc.hook_add(UC_HOOK_CODE, code, begin=addr, end=addr)

        def written(cpu, access, addr, size, value, user):
            pc = cpu.reg_read(UC_ARM_REG_PC)
            if frame <= addr < frame + frame_size:
                counts["image_written_bytes"] += size
                if struct.unpack("<I", cpu.mem_read(syms["g_image_frozen"][0], 4))[0]: counts["writes_after_freeze"] += 1
                if addr + size > frame + frame_size: counts["generator_out_of_bounds"] += 1
            elif generate <= pc < generate + generate_size and not (0x20030000 <= addr and addr + size <= 0x20040000):
                counts["generator_out_of_bounds"] += 1

        uc.hook_add(UC_HOOK_MEM_WRITE, written, begin=0x20000000, end=0x2003ffff)
        error = None
        try: uc.emu_start(entry | 1, 0x01000000, count=100000000, timeout=30000000)
        except Exception as exc: error = str(exc)
        result = struct.unpack("<6I", uc.mem_read(syms["g_result"][0], 24))
        passed = not error and uc.reg_read(UC_ARM_REG_PC) == 0x01000000 and uc.reg_read(UC_ARM_REG_R0) == 0 and result[0] == 0
        passed = passed and counts["writes_after_freeze"] == 0 and counts["generator_out_of_bounds"] == 0 and counts["register_class"] == 1
        if mode == 0: passed = passed and counts["generate"] == 0 and counts["submit_frame"] == 0 and counts["image_written_bytes"] == 0
        elif case_id not in [24,25]: passed = passed and counts["generate"] == 1 and counts["image_written_bytes"] == 38400
        if case_id in [10,11,13,26]: passed = passed and counts["submit_frame"] == 0
        count = struct.unpack("<I", uc.mem_read(syms["g_packet_count"][0], 4))[0]
        trace = {key: list(struct.unpack(f"<{count}I", uc.mem_read(syms[symbol][0], count * 4))) if count else [] for key, symbol in [("lengths","g_lengths"),("headers","g_headers"),("sofs","g_sofs")]}
        rows.append({"id":case_id,"name":name,"mode":"UVC" if mode else "TEST","pass":passed,"assertions":result[1],"failed_line":result[0],"actual":result[2],"expected":result[3],"error":error,"trace":counts,"packets":trace,"image_crc32":f"{zlib.crc32(uc.mem_read(frame,frame_size)):08X}" if frame_size else None})
        print(case_id, name, "PASS" if passed else "FAIL", result[:4], counts, error)

changed = [str(p.relative_to(ROOT)) for p in sources if hashlib.sha256(p.read_bytes()).hexdigest() != before[str(p.relative_to(ROOT))]]
summary = {"total":len(rows),"passed":sum(r["pass"] for r in rows),"failed":sum(not r["pass"] for r in rows)}
(HERE / "results.json").write_text(json.dumps({"summary":summary,"source_changed":changed,"cases":rows},ensure_ascii=False,indent=2),encoding="utf-8")
print(summary, "source_changed", changed)
raise SystemExit(bool(summary["failed"] or changed))
