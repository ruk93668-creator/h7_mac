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

files = ["CM7/Platform/usb_mode.c", "CM7/Platform/usb_mode.h",
         "CM7/Platform/usb_device.c", "CM7/BSP/bsp_usb.c"]
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
for inc in ["CM7/Core/Inc", "CM7/BSP", "CM7/Platform", "CM7/Framework", "Common/Inc",
            "Drivers/STM32H7xx_HAL_Driver/Inc", "Drivers/CMSIS/Include",
            "Drivers/CMSIS/Device/ST/STM32H7xx/Include",
            "Middlewares/ST/STM32_USB_Device_Library/Core/Inc"]:
    args += ["-I", ROOT / inc]
objects = []
for source in [HERE / "mode_test.c"] + list((ROOT / "Middlewares/ST/STM32_USB_Device_Library/Core/Src").glob("*.c")):
    obj = HERE / (source.stem + ".o")
    run(args + ["-c", source, "-o", obj])
    objects.append(obj)
run(["D:/32/ARM/ARMCC/Bin/armlink.exe", *objects, "--entry", "Mode_TestRun", "--scatter",
     ROOT / "Tests/UsbDeviceStage1/test.sct", "--keep=*(.framework_init)", "--no_scanlib",
     "-o", HERE / "mode_test.axf"])
with (HERE / "mode_test.axf").open("rb") as f:
    elf = ELFFile(f)
    entry = elf.header["e_entry"]
    symbols = {s.name: s["st_value"] for s in elf.get_section_by_name(".symtab").iter_symbols()}
    sections = [(s["sh_addr"], s.data()) for s in elf.iter_sections()
                if s["sh_flags"] & 2 and s["sh_type"] == "SHT_PROGBITS" and s["sh_size"]]

legacy = json.loads((ROOT / "Tests/UsbDeviceStage1/results.json").read_text(encoding="utf-8"))["cases"]
cases = [(c["id"], c["name"]) for c in legacy]
cases[4] = (4, "HSI48未就绪（适配当前BSP检查）")
mode_specs = [
    ("未初始化拒绝启动", "未Init", "INVALID_STATE，无硬件调用"),
    ("Init幂等且不启动Core", "重复Init", "TEST/IDLE，零启动"),
    ("RUNNING拒绝重复Start", "已配置后Start", "BUSY，仅启动一次"),
    ("重复Init保留运行状态", "RUNNING再次Init", "保持RUNNING"),
    ("重连请求只记录", "RUNNING请求TEST", "STOPPING，尚未Stop"),
    ("RUNNING主循环无额外操作", "Process一千次", "保持RUNNING，无额外Start/Stop"),
    ("首次启动失败锁存", "HAL Start返回BUSY", "ERROR，保留原错误，不自动重试"),
    ("Core未初始化", "仅Mode Init后Start", "INVALID_STATE及ERROR"),
    ("UVC/MSC未支持", "修改私有mode模拟未实现模式", "NOT_SUPPORTED，零硬件启动"),
    ("Initcall等级顺序", "DEVICE/SERVICE/APPLICATION", "启动一次，等待CONFIGURED"),
    ("产品名称与配置请求", "GET字符串并SET_CONFIGURATION", "38字节名称，进入RUNNING"),
    ("REGISTERING仍为占位", "REGISTERING调用Process", "无硬件动作"),
    ("枚举等待4999ms", "启动tick100，当前5099", "继续WAIT_CONFIGURED"),
    ("枚举等待5000ms", "启动tick100，当前5100", "ERROR/TIMEOUT"),
    ("期限边界配置完成", "5000ms前配置完成再Process", "RUNNING/SYS_OK"),
    ("底层错误优先", "等待期间底层BUSY错误", "ERROR且保留BUSY"),
    ("Stop成功", "1000ms执行Stop", "WAIT_RECONNECT，时间1000，仅停止一次"),
    ("Stop失败", "HAL Stop返回ERROR", "ERROR/IO，不自动重试"),
    ("断开499ms", "1000停止，1499处理", "继续WAIT_RECONNECT"),
    ("断开500ms", "1000停止，1500处理", "STARTING，本轮不Start"),
    ("重启更新时间戳", "1000停止，1500重启", "WAIT_CONFIGURED，时间戳1500"),
    ("重启享有完整枚举窗口", "1500重启，6499处理", "4999ms，仍等待配置"),
    ("重启失败进入ERROR", "重启HAL返回BUSY", "ERROR，保留BUSY"),
    ("重启失败不逐轮重试", "重启失败再Process", "Start总计两次，不再自动启动"),
    ("ERROR当前拒绝恢复请求", "ERROR请求TEST重连", "BUSY，记录当前限制"),
    ("WAIT_CONFIGURED拒绝重连", "未配置时请求TEST", "BUSY，无Stop"),
    ("不接受UVC/MSC目标", "RUNNING请求UVC及MSC", "NOT_SUPPORTED，不改变状态"),
    ("拒绝重复重连请求", "STOPPING再次请求", "BUSY，无额外硬件动作"),
    ("断开间隔跨Tick回绕", "0xFFFFFF00开始等499/500ms", "499ms等待，500ms进入STARTING"),
    ("枚举超时跨Tick回绕", "0xFFFFFF00启动等4999/5000ms", "4999ms等待，5000msTIMEOUT"),
    ("TEST完整重连", "停止、等待、重启、主机配置", "一停两启，回到RUNNING"),
    ("重连后产品身份", "TEST重连后GET产品字符串", "原38字节产品名称仍可读取"),
]
cases += [(100+i, spec[0]) for i, spec in enumerate(mode_specs)]
rows = []
for case_id, name in cases:
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    for address, size in [(0x08000000, 0x100000), (0x20000000, 0x40000),
                          (0xD0000000, 0x20000), (0x01000000, 0x1000), (0x58020000, 0x10000)]:
        uc.mem_map(address, size)
    for address, data in sections:
        uc.mem_write(address, data)
    uc.reg_write(UC_ARM_REG_SP, 0x2003FFF0)
    uc.reg_write(UC_ARM_REG_LR, 0x01000001)
    uc.reg_write(UC_ARM_REG_R0, case_id)
    error = None
    try:
        uc.emu_start(entry | 1, 0x01000000, count=100000000, timeout=10000000)
    except Exception as exc:
        error = str(exc)
    values = struct.unpack("<6I", uc.mem_read(symbols["g_result"], 24))
    passed = not error and uc.reg_read(UC_ARM_REG_PC) == 0x01000000 and uc.reg_read(UC_ARM_REG_R0) == 0 and values[0] == 0
    observation = dict(zip(["state", "mode", "target_mode", "last_result", "state_enter_ms", "tick", "start_calls", "stop_calls", "core_state", "core_result", "nvic", "initialized"], struct.unpack("<12I", uc.mem_read(symbols["g_mode_observation"], 48))))
    state_names = ["UNINITIALIZED", "IDLE", "STARTING", "WAIT_CONFIGURED", "RUNNING", "STOPPING", "WAIT_RECONNECT", "REGISTERING", "ERROR"]
    observation["state_name"] = state_names[observation["state"]] if observation["state"] < len(state_names) else "UNKNOWN"
    for key in ["last_result", "core_result"]:
        if observation[key] >= 0x80000000:
            observation[key] -= 0x100000000
    spec = mode_specs[case_id-100] if case_id >= 100 else (name, "沿用既有USB测试输入", "既有断言全部通过")
    rows.append({"input": spec[1], "expected": spec[2], "actual": observation, "id": case_id, "name": name, "result": "PASS" if passed else "FAIL",
                 "checks": values[1], "fail_line": values[0], "error": error})
    print(f"{case_id:03d} {rows[-1]['result']} {name}: checks={values[1]}, fail_line={values[0]}, error={error}")
summary = {"total": len(rows), "passed": sum(r["result"] == "PASS" for r in rows),
           "failed": sum(r["result"] == "FAIL" for r in rows), "assertions": sum(r["checks"] for r in rows)}
changed = [f for f, sha in before.items() if hashlib.sha256((ROOT / f).read_bytes()).hexdigest() != sha]
(HERE / "results.json").write_text(json.dumps({"summary": summary, "source_changed": changed, "source_sha256": before, "cases": rows}, ensure_ascii=False, indent=2), encoding="utf-8")
lines = ["# USB Mode Manager 当前源码模拟验收", "", str(summary), "", "|编号|场景|输入|预期|实际|结果|", "|---|---|---|---|---|---|"]
for row in rows:
    lines.append("|"+"|".join([str(row["id"]), row["name"], row["input"], row["expected"], str(row["actual"]), row["result"]])+"|")
(HERE / "results.md").write_text("\n".join(lines)+"\n", encoding="utf-8")
print("TOTAL", summary, "source changed:", changed)
raise SystemExit(bool(summary["failed"] or changed))
