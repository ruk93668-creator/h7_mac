"""Compile and execute V3 startup tests using the existing Cortex-M test harness."""

from pathlib import Path
import argparse
import importlib.util
import json
import os


ROOT = Path(__file__).resolve().parents[2]
V3 = ROOT / "CM7/ApplicationV3"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("--board-smoke", choices=("off", "camera", "usb", "storage"), default="off")
    parser.add_argument("--board-stress", choices=("off", "repeat", "recovery"), default="off")
    args = parser.parse_args()
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
    # C1's Python runner only supplies ARMCC5 linking and Cortex-M execution.
    legacy_includes = {ROOT / "CM7/Application", ROOT / "CM7/Application/Tasks"}
    harness.cc = [flag for index, flag in enumerate(harness.cc)
                  if flag not in legacy_includes and
                  not (flag == "-I" and harness.cc[index + 1] in legacy_includes)]
    harness.cc.extend(("-I", V3 / "Core", "-I", V3 / "Tasks", "-I", V3 / "Runtime", "--split_sections"))
    smoke_selection = ("off", "camera", "usb", "storage").index(args.board_smoke)
    stress_selection = ("off", "repeat", "recovery").index(args.board_stress)
    harness.cc.extend((f"-DAPP_V3_BOARD_SMOKE={smoke_selection}", f"-DAPP_V3_BOARD_STRESS={stress_selection}"))
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
                scenarios.append(61)
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
