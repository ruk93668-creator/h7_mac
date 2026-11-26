"""Compile and execute V3 startup tests using the existing Cortex-M test harness."""

from pathlib import Path
import argparse
import importlib.util
import json
import os
import sys


ROOT = Path(__file__).resolve().parents[2]
V3 = ROOT / "CM7/ApplicationV3"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("--production", action="store_true")
    parser.add_argument("--board-smoke", choices=("off", "camera", "usb", "storage"), default="off")
    parser.add_argument("--board-stress", choices=("off", "repeat", "recovery"), default="off")
    parser.add_argument("--raw-uvc", action="store_true")
    parser.add_argument("--record-board", action="store_true")
    parser.add_argument("--joint-board", action="store_true")
    args = parser.parse_args()
    if args.production and (args.board_smoke != "off" or args.board_stress != "off" or
                            args.record_board or args.joint_board):
        parser.error("--production is independent of the board profiles")
    if not args.production and not (V3 / "Tasks/board_smoke.h").is_file():
        parser.error("prepare the isolated tree with BoardProfiles/prepare.py, or select --production")
    if args.joint_board:
        args.record_board = True
    if args.record_board and (args.raw_uvc or getattr(args, "raw_board", False) or args.board_smoke != "off"):
        parser.error("--record-board selects an independent profile")
    if args.board_stress != "off" and args.board_smoke == "off":
        parser.error("--board-stress requires a board smoke scenario")
    if args.board_stress == "recovery" and args.board_smoke == "camera":
        parser.error("recovery stress is available for USB and Storage")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    os.environ["C1_TEST_OUTPUT_ROOT"] = str(output)

    spec = importlib.util.spec_from_file_location(
        "cortex_test", ROOT / "Tests/CameraLifecycleC1/run_tests.py"
    )
    harness = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(harness)
    harness.EXTRA_REGIONS = [(0xD0000000, 0x800000)]
    # C1's Python runner only supplies ARMCC5 linking and Cortex-M execution.
    legacy_includes = {ROOT / "CM7/Application", ROOT / "CM7/Application/Tasks"}
    harness.cc = [flag for index, flag in enumerate(harness.cc)
                  if flag not in legacy_includes and
                  not (flag == "-I" and harness.cc[index + 1] in legacy_includes)]
    sys.path.insert(0, str(ROOT / "Tests/Ethernet"))
    from e4_sources import PORT, TRANSPORT, SERVICE, LWIP, LWIP_DHCP, LWIP_TCP
    harness.cc.extend(("-I", ROOT / "Platform/Ethernet",
                       "-I", ROOT / "ThirdParty/lwip/src/include",
                       "-I", ROOT / "CM7/Service/Ethernet",
                       "-DETH_NETWORK_SERVICE"))
    harness.cc.extend(("-I", V3 / "Core", "-I", V3 / "Tasks", "-I", V3 / "Runtime", "--split_sections"))
    if args.production:
        harness.cc.extend(("-I", Path(__file__).with_name("BoardProfiles"), "-DAPP_V3_VERIFY_PRODUCT=1"))
    smoke_selection = ("off", "camera", "usb", "storage").index(args.board_smoke)
    stress_selection = ("off", "repeat", "recovery").index(args.board_stress)
    harness.cc.extend((f"-DAPP_V3_BOARD_SMOKE={smoke_selection}", f"-DAPP_V3_BOARD_STRESS={stress_selection}"))
    if args.raw_uvc:
        harness.cc.append("-DAPP_V3_RAW_UVC_TEST=1")
    if args.record_board:
        harness.cc.extend(("-DAPP_V3_RECORD_BOARD_TEST=1", "-DAPP_VIDEO_MJPEG_MODE=1", "-DAPP_STORAGE_PARTITION=1"))
    if args.joint_board:
        harness.cc.append("-DAPP_V3_JOINT_BOARD_TEST=1")
    kernel = ROOT / "Middlewares/Third_Party/FreeRTOS/Source"
    real_run = harness.run

    def run_with_queue_fault_seam(command):
        # Only the host-side kernel build exposes the original send symbol to the fixture.
        if kernel / "queue.c" in command:
            command = [*command, "-DxQueueGenericSend=Test_RealQueueSend"]
        if V3 / "Runtime/service_event_loop.c" in command:
            command = [*command, "-DAppV3EventLoop_Subscribe=Test_RealEventSubscribe"]
        return real_run(command)

    harness.run = run_with_queue_fault_seam
    sources = [
        Path(__file__).with_name("startup_test.c"),
        Path(__file__).with_name("hardware_fixture.c"),
        V3 / "Core/app_main.c",
        V3 / "Core/app_tasks.c",
        V3 / "Core/app_services.c",
        V3 / "Core/app_messages.c",
        V3 / "Services/Camera/camera_service.c",
        V3 / "Services/Storage/storage_service.c",
        V3 / "Services/USB/usb_service.c",
        V3 / "Services/Network/network_service.c",
        V3 / "Services/Network/network_frames.c",
        *[ROOT / p for p in PORT + TRANSPORT + SERVICE + LWIP + LWIP_DHCP + LWIP_TCP],
        Path(__file__).with_name("network_fixture.c"),
        ROOT / "Tests/Ethernet/network_board_acceptance.c",
        V3 / "Tasks/service_tasks.c",
        *sorted((V3 / "Runtime").glob("*.c")),
        kernel / "queue.c",
        kernel / "list.c",
    ]
    results = []
    for optimization in ("O0", "O2"):
        harness.cc = ["-" + optimization if flag in ("-O0", "-O2") else flag for flag in harness.cc]
        name = "app_v3_" + optimization
        harness.build(name, sources, "AppV3_TestRun")
        scenarios = range(53, 58) if smoke_selection else range(53)
        if stress_selection:
            scenarios = list(range(58, 61 if stress_selection == 2 else 60))
            if args.board_smoke == "usb" and args.board_stress == "repeat":
                scenarios.extend((61, 62))
        if args.board_smoke in ("off", "usb"):
            scenarios = [*scenarios, *range(63, 70)]
        if not smoke_selection:
            scenarios = [*scenarios, *range(70, 75)]
        if not smoke_selection:
            scenarios = [*scenarios, *range(88, 96)]
        if args.raw_uvc:
            scenarios = range(75, 83)
        if args.record_board:
            scenarios = range(96, 98)
        if args.joint_board:
            scenarios = range(98, 104)
        if args.production and not args.raw_uvc:
            scenarios = [*range(24), 28, 29, *range(38, 53), *range(63, 75),
                         *range(88, 96), 104, *range(105, 127)]
        for scenario in scenarios:
            row = harness.execute(name, "AppV3_TestRun", scenario)
            if scenario < 49:
                row.pop("trace", None)
            results.append({"optimization": optimization, "id": scenario, **row})
            print(results[-1], flush=True)

    (output / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    return any(not row["pass"] for row in results)


if __name__ == "__main__":
    raise SystemExit(main())
