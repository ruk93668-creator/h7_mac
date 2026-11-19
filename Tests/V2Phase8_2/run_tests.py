"""使用 ARMCC5 和真实 FreeRTOS 队列、TCB、通知验证 P8.2 运输。"""
import datetime
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
OUT = ROOT / 'output/P8_2' / ('test_' + datetime.datetime.now().strftime('%Y%m%d_%H%M%S_%f'))
OUT.mkdir(parents=True)
os.environ['C1_TEST_OUTPUT_ROOT'] = str(OUT)
sys.path[:0] = [str(ROOT / 'Tests/CameraLifecycleC1'), str(ROOT / 'output/V2Phase1/python_deps')]
import run_tests as arm
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_SP

v2 = ROOT / 'CM7/ApplicationV2'
kernel = ROOT / 'Middlewares/Third_Party/FreeRTOS/Source'
native = OUT / 'unbound_native.c'
lines = (ROOT / 'Tests/V2Phase4/native_dependencies.c').read_text().splitlines(keepends=True)
fixture_overrides = ('void Platform_TimeGetMs(', 'void BSP_DCMI_SetEventCallback(')
# 两个端口由本专项夹具提供，其余硬件入口继续使用原陷阱。
native.write_text(''.join(line for line in lines if not line.startswith(fixture_overrides)))
file_backend = v2 / 'Modules/Storage/storage_backend.c'
sources = [
    HERE / 'rtos_test.c', *sorted(path for path in v2.rglob('*.c') if path != file_backend), native,
    ROOT / 'CM7/Device/Src/ov5640_device.c', ROOT / 'CM7/Media/Src/camera_capture.c',
    ROOT / 'CM7/Service/usb_uvc_desc.c', ROOT / 'CM7/Application/app_camera_lifecycle.c',
    ROOT / 'CM7/Application/app_camera_lifecycle_replay.c', ROOT / 'CM7/Application/app_camera_qualification.c',
    ROOT / 'Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_cortex.c',
    kernel / 'queue.c', kernel / 'list.c', kernel / 'portable/RVDS/ARM_CM7/r0p1/port.c',
]
arm.cc += ['--split_sections', '-I', ROOT / 'CM7/Device/Components/OV5640', '-I', ROOT / 'Middlewares/Third_Party/FatFs']


def execute(image, case):
    """按指定用例执行镜像，记录角色栈深和 IPC 等待证据。"""
    symbols, sections = arm.images[image]
    cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    for address, length in [(0x08000000, 0x100000), (0x20000000, 0x80000),
                            (0x01000000, 0x1000), (0xE000E000, 0x2000)]:
        cpu.mem_map(address, length)
    for address, data in sections:
        cpu.mem_write(address, data)
    start_sp = 0x2007fff0
    lowest = start_sp
    cpu.reg_write(UC_ARM_REG_SP, start_sp)
    cpu.reg_write(UC_ARM_REG_LR, 0x01000001)
    cpu.reg_write(UC_ARM_REG_R0, case)
    cpu.mem_write(0xE000ED0C, struct.pack('<I', 3 << 8))
    cpu.mem_write(symbols['uxCriticalNesting'], b'\0' * 4)
    delay = None
    trapped = None
    active_round = None
    role_stack = {}

    def observe(emulator, address, _size, _context):
        # 以原任务钩子的入口 SP 为基准，排除夹具外层栈帧。
        nonlocal lowest, delay, trapped, active_round
        stack = emulator.reg_read(UC_ARM_REG_SP)
        lowest = min(lowest, stack)
        if active_round is not None:
            role, entry_sp, return_pc = active_round
            role_stack[role] = max(role_stack.get(role, 0), entry_sp - stack)
            if address == return_pc:
                active_round = None
        if address == (symbols['V2Runtime_Round'] & ~1):
            active_round = (emulator.reg_read(UC_ARM_REG_R0), stack,
                            emulator.reg_read(UC_ARM_REG_LR) & ~1)
        if address == (symbols['Platform_RtosAssert'] & ~1):
            trapped = emulator.reg_read(UC_ARM_REG_R1)
            emulator.emu_stop()
        if case == 5 and address == (symbols['vTaskDelay'] & ~1):
            delay = emulator.reg_read(UC_ARM_REG_R0)
            emulator.emu_stop()

    cpu.hook_add(UC_HOOK_CODE, observe)
    error = None
    try:
        cpu.emu_start(symbols['P82_Run'] | 1, 0x01000000, count=12_000_000)
    except Exception as exc:
        error = str(exc)
    failure = struct.unpack('<I', cpu.mem_read(symbols['g_failure'], 4))[0]
    starts = struct.unpack('<I', cpu.mem_read(symbols['g_ipc_starts'], 4))[0]
    steps = struct.unpack('<I', cpu.mem_read(symbols['g_ipc_steps'], 4))[0]
    debug = struct.unpack('<IIiiIIII', cpu.mem_read(symbols['g_v2_runtime_debug'], 32))
    completed = (cpu.reg_read(UC_ARM_REG_PC) == 0x01000000 and cpu.reg_read(UC_ARM_REG_R0) == 0)
    if case == 5:
        completed = delay == 100 and starts == steps == 1 and debug == (0,) * 8
    return {'pass': completed and not failure and error is None and trapped is None,
            'failure': failure, 'error': error, 'trap': trapped, 'debug': debug,
            'stack': start_sp - lowest, 'role_stack': role_stack, 'ipc_delay': delay, 'ipc_starts': starts, 'ipc_steps': steps}


if __name__ == '__main__':
    rows = []
    for optimization in ('O0', 'O2'):
        arm.cc = ['-' + optimization if flag in ('-O0', '-O2') else flag for flag in arm.cc]
        image = 'p82_' + optimization
        arm.run(arm.cc + ['-c', file_backend, '-o', OUT / ('isolated_fs_' + optimization + '.o')])
        arm.build(image, sources, 'P82_Run')
        for case in range(6):
            row = {'opt': optimization, 'case': case, **execute(image, case)}
            rows.append(row)
            print(row, flush=True)
    (OUT / 'results.json').write_text(json.dumps({
        'head': subprocess.check_output(['git','rev-parse','HEAD'], text=True).strip(),
        'files': {
            str(source_path.relative_to(ROOT)): hashlib.sha256(source_path.read_bytes()).hexdigest()
            for source_path in [*sources, file_backend]
        },
        'rows': rows,
    }, indent=2), encoding='utf-8')
    print(OUT)
    raise SystemExit(any(not row['pass'] for row in rows))
