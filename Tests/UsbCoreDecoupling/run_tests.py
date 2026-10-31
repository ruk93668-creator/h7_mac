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
         "CM7/Platform/usb_device.c", "CM7/BSP/bsp_usb.c",
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
for source in [HERE / "core_test.c"] + list((ROOT / "Middlewares/ST/STM32_USB_Device_Library/Core/Src").glob("*.c")):
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
    ("Core就绪后Mode初始化幂等", "先Init Core，再重复Mode Init", "默认TEST仅注册一次，零硬件启动"),
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
    ("ERROR允许显式请求受控恢复", "ERROR请求TEST重连", "BUSY，记录当前限制"),
    ("WAIT_CONFIGURED拒绝重连", "未配置时请求TEST", "BUSY，无Stop"),
    ("不接受UVC/MSC目标", "RUNNING请求UVC及MSC", "NOT_SUPPORTED，不改变状态"),
    ("拒绝重复重连请求", "STOPPING再次请求", "BUSY，无额外硬件动作"),
    ("断开间隔跨Tick回绕", "0xFFFFFF00开始等499/500ms", "499ms等待，500ms进入STARTING"),
    ("枚举超时跨Tick回绕", "0xFFFFFF00启动等4999/5000ms", "4999ms等待，5000msTIMEOUT"),
    ("TEST完整重连", "停止、等待、重启、主机配置", "一停两启，回到RUNNING"),
    ("重连后产品身份", "TEST重连后GET产品字符串", "原38字节产品名称仍可读取"),
]
cases += [(100+i, spec[0]) for i, spec in enumerate(mode_specs)]
core_specs = [('无Class禁止Start', '执行Core_RunCase(200)，参数见core_test.c', '未注册且零硬件Start'),
 ('Initcall保留TEST启动', '执行Core_RunCase(201)，参数见core_test.c', 'SERVICE注册，APPLICATION只启动一次'),
 ('配置总长不匹配拒绝', '执行Core_RunCase(202)，参数见core_test.c', '原注册对象保持不变'),
 ('bLength为0拒绝', '执行Core_RunCase(203)，参数见core_test.c', '拒绝无限循环或越界描述符'),
 ('端点数量不匹配拒绝', '执行Core_RunCase(204)，参数见core_test.c', '拒绝声明与实际端点数量不同'),
 ('外部配置返回及EP0截断', '执行Core_RunCase(205)，参数见core_test.c', '静态指针25字节，主机请求255只返回25'),
 ('运行中禁止换Class', '执行Core_RunCase(206)，参数见core_test.c', '返回INVALID_STATE，当前配置不变'),
 ('Class不能占用EP0', '执行Core_RunCase(207)，参数见core_test.c', '拒绝Class关闭或申请控制端点'),
 ('端点申请与状态快照', '执行Core_RunCase(208)，参数见core_test.c', 'is_used、opened、类型、最大包长一致'),
 ('重复申请拒绝', '执行Core_RunCase(209)，参数见core_test.c', '返回ALREADY_EXISTS，不重复操作硬件'),
 ('通用Bulk和其他IN端点可用', '执行Core_RunCase(210)，参数见core_test.c', '已精确声明的Bulk和Interrupt端点正常打开'),
 ('关闭状态禁止发送', '执行Core_RunCase(211)，参数见core_test.c', '不调用HAL发送'),
 ('busy防覆盖且仍已配置', '执行Core_RunCase(212)，参数见core_test.c', '返回BUSY，原事务仍busy，设备保持CONFIGURED'),
 ('完成回调释放busy', '执行Core_RunCase(213)，参数见core_test.c', '先清busy，再调用外部DataIn'),
 ('关闭后忽略迟到完成', '执行Core_RunCase(214)，参数见core_test.c', '不再调用已释放端点的DataIn'),
 ('关闭取消事务并释放资源', '执行Core_RunCase(215)，参数见core_test.c', 'opened/busy清除，EP0保留'),
 ('Flush和Close资源所有权', '活动IN及同编号OUT，观察HAL顺序与上下文', 'busy Flush不执行；Abort及Flush先于Close，OUT所有权保持'),
 ('非法端点地址拒绝', '执行Core_RunCase(217)，参数见core_test.c', '拒绝保留地址位及空状态指针'),
 ('INOUT方向检查', '执行Core_RunCase(218)，参数见core_test.c', 'Transmit仅IN，Receive仅OUT'),
 ('未完成事件转交Class', '执行Core_RunCase(219)，参数见core_test.c', 'busy清除，记录TIMEOUT并回调外部IsoINIncomplete'),
 ('HAL失败不残留busy', '执行Core_RunCase(220)，参数见core_test.c', '保留HAL BUSY错误且busy清除'),
 ('关闭失败保留所有权', '执行Core_RunCase(221)，参数见core_test.c', 'opened/busy保持，禁止错误释放缓冲区'),
 ('Stop清理与重启', '执行Core_RunCase(222)，参数见core_test.c', '所有数据资源释放，重启不自动打开数据端点'),
 ('Stop失败锁存及重试', '执行Core_RunCase(223)，参数见core_test.c', '停止失败不DeInit，禁止Start/Register，重试成功可恢复'),
 ('复位重建EP0并清理数据端点', '执行Core_RunCase(224)，参数见core_test.c', 'EP0双向重开，非EP0释放'),
 ('断线清理资源', '执行Core_RunCase(225)，参数见core_test.c', '非EP0资源释放'),
 ('未知端点请求STALL', '执行Core_RunCase(226)，参数见core_test.c', '通过EP0 STALL拒绝，不访问无效端点'),
 ('等时HALT请求STALL', '执行Core_RunCase(227)，参数见core_test.c', '控制传输STALL，不对等时端点做HALT'),
 ('保留请求类型STALL', '执行Core_RunCase(228)，参数见core_test.c', '不分发给Class'),
 ('Class失败自动STALL', '执行Core_RunCase(229)，参数见core_test.c', '即使Class只返回FAIL，Core也执行STALL'),
 ('Class通用Setup入口', '执行Core_RunCase(230)，参数见core_test.c', '调用外部Setup并完成零长度状态阶段'),
 ('缺少必要回调拒绝', '执行Core_RunCase(231)，参数见core_test.c', '保持旧注册对象不变'),
 ('空length与未注册配置', '执行Core_RunCase(232)，参数见core_test.c', '返回NULL，未注册时长度为0'),
 ('异设备句柄拒绝', '执行Core_RunCase(233)，参数见core_test.c', '不操作其他设备的端点'),
 ('等时端点拒绝STALL接口', '执行Core_RunCase(234)，参数见core_test.c', '返回NOT_SUPPORTED'),
 ('完成回调内重新提交', '执行Core_RunCase(235)，参数见core_test.c', '回调内可再提交，busy正确恢复true'),
 ('HAL返回前完成中断延迟交付', '执行Core_RunCase(236)，参数见core_test.c', '屏蔽USB IRQ直到提交，回调后busy为false'),
 ('关闭期间迟到完成被忽略', '执行Core_RunCase(237)，参数见core_test.c', '资源释放后迟到事件不调用Class'),
 ('恢复原USB IRQ及PRIMASK', '执行Core_RunCase(238)，参数见core_test.c', '原USB IRQ关闭时保持关闭，不屏蔽全局IRQ'),
 ('设备描述符长度错误拒绝', '执行Core_RunCase(239)，参数见core_test.c', '拒绝17字节设备描述符，保留旧配置'),
 ('已验证配置不被换getter影响', '执行Core_RunCase(240)，参数见core_test.c', '官方Core使用注册时缓存，不再次取变更getter'),
 ('忽略其他PCD的错误回调', '执行Core_RunCase(241)，参数见core_test.c', '不转交外部Class'),
 ('EP0打开失败锁存恢复要求', 'HAL Open返回ERROR', '禁止重启，Stop成功后可恢复'),
 ('Reset失败仍清理数据端点', '外部DeInit返回FAIL', '非EP0端点释放，故障锁存'),
 ('释放回调拒绝生命周期重入', 'DeInit内调用Init/Start/Stop/Register', '四项均BUSY，硬件保持停止'),
 ('注册回调拒绝生命周期重入', '配置getter内调用生命周期接口', '四项均BUSY，尚未启动硬件'),
 ('新SETUP清理EP0状态', '先STALL，再发新控制请求', 'EP0 stalled快照清零'),
 ('Start过程拒绝嵌套Stop', 'HAL Start中重入Stop', '返回BUSY，启动正常完成')]
cases += [(200+i, spec[0]) for i, spec in enumerate(core_specs)]
import re
stage2_specs = {int(case): (name.strip(), data.strip(), expected.strip())
                for case, name, data, expected in re.findall(
                    r"//CASE (\d+) \| ([^|\n]+) \| ([^|\n]+) \| ([^\n]+)",
                    (HERE / "endpoint_lifecycle_test.c").read_text(encoding="utf-8"))}
assert set(stage2_specs) == set(range(300, 372))
cases += [(case, spec[0]) for case, spec in sorted(stage2_specs.items())]
rows = []
for case_id, name in cases:
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    for address, size in [(0x08000000, 0x100000), (0x20000000, 0x40000),
                          (0xD0000000, 0x20000), (0x01000000, 0x1000), (0x58020000, 0x10000), (0xE000E000, 0x2000), (0x40080000, 0x40000)]:
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
    spec = stage2_specs[case_id] if case_id >= 300 else core_specs[case_id-200] if case_id >= 200 else mode_specs[case_id-100] if case_id >= 100 else (name, "沿用既有USB测试输入；显式注册迁移后的TEST", "既有断言通过；端点BUSY不再清除CONFIGURED状态")
    if case_id >= 300:
        observation["stage2"] = dict(zip(
            ["class_state", "pool_used", "class_init", "class_deinit", "data_in", "data_out",
             "iso_incomplete_in", "iso_incomplete_out", "rx_words", "fifo_words", "bsp_started", "stop_required"],
            struct.unpack("<12I", uc.mem_read(symbols["g_stage2_observation"], 48))))
    if case_id == 371: observation["control"] = list(struct.unpack("<12I",uc.mem_read(symbols["g_control_observation"],48)))
    rows.append({"input": spec[1], "expected": spec[2], "actual": observation, "id": case_id, "name": name, "result": "PASS" if passed else "FAIL",
                 "failure_actual": values[2] if not passed else None, "failure_expected": values[3] if not passed else None,
                 "checks": values[1], "fail_line": values[0], "error": error})
    print(f"{case_id:03d} {rows[-1]['result']} {name}: checks={values[1]}, fail_line={values[0]}, error={error}")
summary = {"total": len(rows), "passed": sum(r["result"] == "PASS" for r in rows),
           "failed": sum(r["result"] == "FAIL" for r in rows), "assertions": sum(r["checks"] for r in rows)}
changed = [f for f, sha in before.items() if hashlib.sha256((ROOT / f).read_bytes()).hexdigest() != sha]
(HERE / "results.json").write_text(json.dumps({"summary": summary, "source_changed": changed, "source_sha256": before, "cases": rows}, ensure_ascii=False, indent=2), encoding="utf-8")
lines = ["# USB Device Core 解耦模拟验收", "", str(summary), "", "|编号|场景|输入|预期|实际|结果|", "|---|---|---|---|---|---|"]
for row in rows:
    lines.append("|"+"|".join([str(row["id"]), row["name"], row["input"], row["expected"], str(row["actual"]), row["result"]])+"|")
(HERE / "results.md").write_text("\n".join(lines)+"\n", encoding="utf-8")
print("TOTAL", summary, "source changed:", changed)
raise SystemExit(bool(summary["failed"] or changed))
