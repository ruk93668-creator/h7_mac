"""ARMCC5 + Unicorn, standalone real payload module, no HAL or USB stack."""
from pathlib import Path
import hashlib
import importlib.metadata
import json
import struct
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8")
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, "D:/32/STM32H7/h7_mac/Tests/OledAsyncAcceptance/python_deps")
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile

ARM = Path("D:/32/ARM/ARMCC/Bin")
NAMES = [
    "38400B准确76包", "连续两帧FID交替", "1B帧", "509B帧", "510B整包帧", "511B跨包帧",
    "1020B恰好两整包", "1021B末包3字节", "最小3B包长", "511B最大包长", "4B包长", "非对齐输入",
    "重复Build不重建不推进", "重复完成无待确认包", "重复旧完成不能推进下一包", "错误与零Token",
    "末包构造不能提前翻FID", "构包前取消", "待确认取消保留引用", "Abort确认后释放取消帧",
    "Cancel与成功完成交错", "包间取消切换帧边界", "重复空闲取消", "重复待确认取消", "未请求取消禁止Abort",
    "错误Abort Token不能释放", "取消后旧完成不能命中新帧", "活动帧Reset拒绝", "待确认包Reset拒绝",
    "取消等待时Reset拒绝", "空闲Reset保留Token", "Reset后旧完成不能命中新包", "零帧长", "空图像指针",
    "包长0", "包长1", "包长2", "包长513", "包长65535", "图像地址溢出", "图像覆盖上下文",
    "图像覆盖内部包", "空输出视图", "输出视图覆盖上下文", "输出视图覆盖只读帧", "未开始帧禁止构包",
    "无待确认包禁止完成", "Init空指针", "其他API空对象", "未初始化对象", "重复Init空闲对象",
    "重复Init不能丢待确认所有权", "最后有效Token后拒绝回绕", "Token耗尽无状态推进", "独立实例互不影响",
    "输入只读且末端边界", "取消等待禁止Build且不写输出", "PIO尾部补齐不越界", "取消末包不重复翻FID",
    "Start不能覆盖活动状态", "损坏偏移保护", "损坏待确认长度保护",
]
assert len(NAMES) == 62
SOURCES = [ROOT / "CM7/Service/usb_uvc_payload.c", ROOT / "CM7/Service/usb_uvc_payload.h"]
before = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in SOURCES}
commands = []

def run(args):
    result = subprocess.run(list(map(str, args)), cwd=ROOT, capture_output=True)
    output = (result.stdout + result.stderr).decode("utf-8", errors="replace")
    commands.append({"command": subprocess.list2cmdline(list(map(str, args))), "returncode": result.returncode, "output": output})
    (HERE / "commands.json").write_bytes(json.dumps(commands, ensure_ascii=False, indent=2).encode("utf-8"))
    if result.returncode:
        raise RuntimeError(output)

run([ARM / "armcc.exe", "--vsn"])
all_rows = []
variants = []
for optimization in ["-O0", "-O2"]:
    objects = []
    for source in [HERE / "payload_test.c", SOURCES[0]]:
        obj = HERE / (source.stem + optimization + ".o")
        run([ARM / "armcc.exe", "--cpu", "Cortex-M7", "--c99", optimization,
             "-I", ROOT / "CM7/Service", "-I", ROOT / "Common/Inc", "-c", source, "-o", obj])
        objects.append(obj)
    axf = HERE / ("payload" + optimization + ".axf")
    run([ARM / "armlink.exe", *objects, "--entry", "Payload_TestRun", "--scatter",
         ROOT / "Tests/UsbDeviceStage1/test.sct", "--no_scanlib", "-o", axf])
    with axf.open("rb") as f:
        elf = ELFFile(f)
        entry = elf.header["e_entry"]
        symbols = {s.name: s["st_value"] for s in elf.get_section_by_name(".symtab").iter_symbols()}
        sections = [(s["sh_addr"], s.data()) for s in elf.iter_sections()
                    if s["sh_flags"] & 2 and s["sh_type"] == "SHT_PROGBITS" and s["sh_size"]]
    rows = []
    for case_id, name in enumerate(NAMES):
        uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        for address, size in [(0x08000000, 0x100000), (0x20000000, 0x40000),
                              (0x30000000, 0x20000), (0x01000000, 0x1000)]:
            uc.mem_map(address, size)
        for address, data in sections: uc.mem_write(address, data)
        uc.reg_write(UC_ARM_REG_SP, 0x2003FFF0)
        uc.reg_write(UC_ARM_REG_LR, 0x01000001)
        uc.reg_write(UC_ARM_REG_R0, case_id)
        counters = {"source_read_bytes": 0, "source_writes": 0}
        violations = []
        def memory_check(cpu, access, address, size, value, data):
            watching = struct.unpack("<I", cpu.mem_read(symbols["g_frame_watch"], 4))[0]
            if not watching or not (address < 0x30020000 and address + size > 0x30000000):
                return
            start = struct.unpack("<I", cpu.mem_read(symbols["g_frame_start"], 4))[0]
            length = struct.unpack("<I", cpu.mem_read(symbols["g_frame_size"], 4))[0]
            from unicorn import UC_MEM_WRITE
            if access == UC_MEM_WRITE:
                counters["source_writes"] += 1
                violations.append({"reason": "image_write", "address": address, "size": size})
            else:
                counters["source_read_bytes"] += size
                if address < start or address + size > start + length:
                    violations.append({"reason": "image_overread", "address": address, "size": size})
        uc.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, memory_check)
        error = None
        try: uc.emu_start(entry | 1, 0x01000000, count=80000000, timeout=20000000)
        except Exception as exc: error = str(exc)
        values = struct.unpack("<4I", uc.mem_read(symbols["g_result"], 16))
        count = struct.unpack("<I", uc.mem_read(symbols["g_trace_count"], 4))[0]
        trace = {}
        if count <= 256:
            for label, symbol in [("lengths", "g_lengths"), ("headers", "g_headers"), ("tokens", "g_tokens")]:
                trace[label] = list(struct.unpack("<%dI" % count, uc.mem_read(symbols[symbol], count * 4))) if count else []
        oracle = True
        if case_id == 0:
            oracle = (trace.get("lengths") == [512] * 75 + [152]
                      and trace.get("headers") == [0x80] * 75 + [0x82]
                      and trace.get("tokens") == list(range(1, 77))
                      and counters["source_read_bytes"] == 38400)
        if case_id == 1:
            oracle = (trace.get("lengths") == ([512] * 75 + [152]) * 2
                      and trace.get("headers") == [0x80] * 75 + [0x82] + [0x81] * 75 + [0x83]
                      and counters["source_read_bytes"] == 76800)
        passed = (not error and not violations and oracle and uc.reg_read(UC_ARM_REG_PC) == 0x01000000
                  and uc.reg_read(UC_ARM_REG_R0) == 0 and values[0] == 0)
        row = {"id": case_id, "name": name, "optimization": optimization, "pass": passed,
               "assertions": values[1], "failed_line": values[0], "actual": values[2], "expected": values[3],
               "error": error, "violations": violations[:10], **counters, "trace": trace}
        rows.append(row)
        print(optimization, case_id, name, "PASS" if passed else "FAIL", values, error, violations[:1], flush=True)
    variants.append({"optimization": optimization, "total": len(rows), "passed": sum(x["pass"] for x in rows)})
    all_rows.extend(rows)
changed = [p for p, h in before.items() if hashlib.sha256((ROOT / p).read_bytes()).hexdigest() != h]
summary = {"unique_cases": len(NAMES), "executions": len(all_rows), "passed": sum(x["pass"] for x in all_rows),
           "failed": sum(not x["pass"] for x in all_rows)}
(HERE / "results.json").write_bytes(json.dumps({"summary": summary, "variants": variants, "source_sha256": before,
                                               "source_changed_during_tests": changed, "cases": all_rows}, ensure_ascii=False, indent=2).encode("utf-8"))
print(summary, flush=True)
raise SystemExit(bool(summary["failed"] or changed))
