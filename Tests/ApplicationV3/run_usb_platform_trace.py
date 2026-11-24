from pathlib import Path
import argparse
import importlib.util
import json
import os

parser = argparse.ArgumentParser()
parser.add_argument("output", type=Path)
root = parser.parse_args().output.resolve()
root.mkdir(parents=True, exist_ok=False)
os.environ["C1_TEST_OUTPUT_ROOT"]=str(root)
spec=importlib.util.spec_from_file_location("harness", "Tests/CameraLifecycleC1/run_tests.py")
h=importlib.util.module_from_spec(spec)
spec.loader.exec_module(h)
from unicorn import Uc
def usb_emulator(*args):
    emulator = Uc(*args)
    emulator.mem_map(0x40000000, 0x100000)
    emulator.mem_map(0x58000000, 0x100000)
    return emulator

h.Uc = usb_emulator
real_run = h.run

def run_with_initcall_region(command):
    if str(command[0]).endswith("armlink.exe"):
        scatter_index = command.index("--scatter") + 1
        command[scatter_index] = Path("Tests/UsbDeviceStage1/test.sct")
        command.append("--keep=*(.framework_init)")
    return real_run(command)

h.run = run_with_initcall_region
h.cc+=["-DAPP_V3_BOARD_SMOKE=2","-DAPP_V3_BOARD_STRESS=1"]
sources=[Path("Tests/ApplicationV3/usb_platform_trace_test.c"),Path("CM7/Platform/platform_sync.c"),
         *sorted(Path("Middlewares/ST/STM32_USB_Device_Library/Core/Src").glob("*.c"))]
rows=[]
for opt in ("O0","O2"):
    h.cc=["-"+opt if arg in ("-O0","-O2") else arg for arg in h.cc]
    h.build(opt,sources,"UsbTrace_TestRun")
    for case in range(2):
        row=dict(optimization=opt,id=case,**h.execute(opt,"UsbTrace_TestRun",case))
        print(row)
        rows.append(row)
(root/"results.json").write_text(json.dumps(rows,indent=2))
raise SystemExit(not all(row["pass"] for row in rows))
