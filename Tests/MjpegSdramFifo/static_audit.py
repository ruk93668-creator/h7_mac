"""Read-only source audit. Never invokes a C compiler or hardware."""
from pathlib import Path
import argparse
import json
import re
import subprocess
import xml.etree.ElementTree as ET
from pycparser import c_parser

ROOT = Path(__file__).resolve().parents[2]
ARGS = argparse.ArgumentParser(description=__doc__)
ARGS.add_argument("--baseline", required=True, help="Explicit source-preservation comparison commit")
BASE = ARGS.parse_args().baseline
RESULTS = []

def source(name):
    data = (ROOT / name).read_bytes()
    try:
        value = data.decode("utf-8")
    except UnicodeDecodeError:
        value = data.decode("gbk")
    return re.sub(r"\r+\n", "\n", value)

def check(name, condition, detail=""):
    RESULTS.append(dict(name=name, status="PASS" if condition else "FAIL", detail=detail))

def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)

def body(text, name):
    match = re.search(r"^(?:static\s+)?(?:SysResult_t|bool|void|uint8_t)\s+" + name +
                      r"\s*\([^;{}]*\)\s*\{", text, re.M)
    if not match:
        raise ValueError(name)
    pos = text.index("{", match.start())
    start, depth = pos, 1
    while depth:
        pos += 1
        depth += (text[pos] == "{") - (text[pos] == "}")
    return text[start:pos + 1]

PREFIX = """typedef unsigned char uint8_t; typedef unsigned short uint16_t;
typedef unsigned int uint32_t; typedef unsigned long long uint64_t;
typedef unsigned long uintptr_t; typedef unsigned long size_t;
typedef int bool; typedef int SysResult_t;
"""
units = {
    "CM7/Media/Src/camera_mjpeg_fifo.c": ["CameraConfig_t", "CameraFrame_t", "BspDcmiTarget_t", "BspDcmiByteStreamOps_t"],
    "CM7/Media/Src/camera_capture.c": ["CameraConfig_t", "CameraFrame_t", "BspDcmiTarget_t", "CameraCaptureEventCallback_t", "CameraCaptureStats_t", "BspDcmiEvents_t"],
    "CM7/BSP/bsp_dcmi.c": ["BspDcmiEventCallback_t", "BspDcmiTarget_t", "BspDcmiFrameCallback_t", "BspDcmiByteStreamOps_t", "BspDcmiEvents_t", "DMA_InitTypeDef", "DCMI_HandleTypeDef", "DMA_HandleTypeDef", "HAL_StatusTypeDef", "GPIO_TypeDef", "GPIO_InitTypeDef", "HAL_DMA_MemoryTypeDef"],
    "CM7/Service/usb_uvc_payload.c": ["UsbUvcPayload_t", "UsbUvcPayloadPacket_t", "VideoFrameView_t"],
    "Common/Inc/video_frame_view.h": [],
    "Tests/MjpegSdramFifo/fifo_test.c": ["CameraConfig_t", "CameraFrame_t", "BspDcmiTarget_t", "BspDcmiByteStreamOps_t", "UsbUvcPayload_t", "UsbUvcPayloadPacket_t", "VideoFrameView_t"],
}
for name, types in units.items():
    text = source(name)
    text = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
    text = re.sub(r"\\\n", "", text)
    text = re.sub(r"^\s*#.*$", "", text, flags=re.M)
    text = re.sub(r"FRAMEWORK_INIT_REGISTER\([\s\S]*?\);", "", text)
    text = re.sub(r"__attribute__\s*\(\(.*?\)\)", "", text)
    prefix = PREFIX + "\n".join("typedef int " + t + ";" for t in types)
    try:
        c_parser.CParser().parse(prefix + "\n" + text)
        check("C99 grammar: " + name, True, "AST grammar only; no real headers, type checking, linking or execution")
    except Exception as exc:
        check("C99 grammar: " + name, False, str(exc))

# Parse the two modified public integration entries without invoking a compiler.
for name, functions, types in [
    ("CM7/Service/usb_uvc_class.c", ["USB_UVC_SubmitFrame", "USB_UVC_SubmitFrameView"],
     ["VideoFrameView_t", "UsbUvcContext_t"]),
    ("CM7/Application/app_uvc_bridge.c", ["App_UvcBridgeUsbProcess"],
     ["VideoFrameView_t", "UsbUvcStreamStatus_t", "AppMessage_t", "CameraFrame_t"]),
]:
    prefix = PREFIX + "\n".join("typedef int " + t + ";" for t in types)
    for function in functions:
        text = "void audited(void)" + body(source(name), function)
        text = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
        try:
            c_parser.CParser().parse(prefix + "\n" + text)
            check("Integration entry grammar: " + function, True, "Grammar only, not type checking")
        except Exception as exc:
            check("Integration entry grammar: " + function, False, str(exc))

# Fixed, mutually disjoint layout inside the existing 4 MiB MPU window.
slots_end = 0xD0000400 + 5 * 640 * 480 * 2
fifo_start, fifo_end, scratch_end = 0xD0300000, 0xD0380000, 0xD0384000
check("YUY2/FIFO/scratch disjoint", slots_end <= fifo_start < fifo_end < scratch_end <= 0xD0400000,
      f"YUY2 end={slots_end:#x}, FIFO=[{fifo_start:#x},{fifo_end:#x}), scratch end={scratch_end:#x}")
header = source("CM7/Media/Inc/camera_mjpeg_fifo.h")
check("Layout macros match plan", all(value in header for value in
      ["0xD0300000UL", "512UL * 1024UL", "8UL * 1024UL", "64U", "8U"]))
module = source("CM7/Media/Src/camera_mjpeg_fifo.c")
check("No image memcpy in FIFO", not re.search(r"\bmemcpy\s*\(", module))
check("No Snapshot or CAPTURE stop in FIFO", "StartJpegSnapshot" not in module and "DCMI_CR_CAPTURE" not in module)
check("No RTOS/USB dependency in FIFO", not re.search(r"FreeRTOS|usb_uvc|TaskHandle_t|xQueue", module))
check("Both ISR callbacks metadata only", all("memcpy" not in body(module, name) and
      "Mjpeg_ParseByte" not in body(module, name) for name in ["Mjpeg_BlockComplete", "Mjpeg_FrameBoundary"]))

for name, functions in {
    "CM7/Media/Src/camera_capture.c": ["CameraJpeg_Validate", "CameraStream_Rotate", "CameraStream_OwnershipValid", "CameraStream_FindReplaceable"],
    "CM7/BSP/bsp_dcmi.c": ["Dcmi_StartSnapshot", "BSP_DCMI_StartContinuous", "BSP_DCMI_ReplaceTarget"],
}.items():
    previous = re.sub(r"\r+\n", "\n", git("show", BASE + ":" + name).decode("gbk"))
    current = source(name)
    for function in functions:
        check("Preserved body: " + function, body(previous, function) == body(current, function))

project = ET.fromstring((ROOT / "MDK-ARM/h7_mac.uvprojx").read_bytes())
for target in project.findall("./Targets/Target"):
    group = next(g for g in target.findall("./Groups/Group") if g.findtext("GroupName") == "CM7\\Media")
    names = [f.findtext("FileName") for f in group.findall("./Files/File")]
    check("Keil module entry: " + target.findtext("TargetName"), names.count("camera_mjpeg_fifo.c") == 1)
    if target.findtext("TargetName") == "h7_mac_CM4":
        check("CM4 excludes CM7 Media group", group.findtext("./GroupOption/CommonProperty/IncludeInBuild") == "0")

protected = ["Drivers", "Middlewares", "CM4", "CM7/Service/usb_uvc_desc.c", "CM7/Service/usb_uvc_desc.h", "CM7/Core/Src/main.c", "CM7/BSP/bsp_sdram.c", "CM7/BSP/bsp_sdram.h"]
changed = git("diff", "--name-only", BASE, "--", *protected).decode().strip()
check("Protected drivers/descriptors/MPU/CM4 unchanged", not changed, changed)
report = dict(kind="static_source_checks_only", base=BASE, results=RESULTS,
              compiler_executed=False, executable_tests_executed=False,
              hardware_verified=False, prepared_executable_cases=14,
              pass_count=sum(r["status"] == "PASS" for r in RESULTS),
              fail_count=sum(r["status"] == "FAIL" for r in RESULTS))
(ROOT / "Tests/MjpegSdramFifo/static_results.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
for row in RESULTS:
    print(row["status"], row["name"], row["detail"])
print("No C compiler, executable tests or hardware invoked.")
raise SystemExit(bool(report["fail_count"]))
