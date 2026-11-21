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
    output = parser.parse_args().output.resolve()
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
    kernel = ROOT / "Middlewares/Third_Party/FreeRTOS/Source"
    sources = [
        Path(__file__).with_name("startup_test.c"),
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
        for scenario in range(18):
            row = harness.execute(name, "AppV3_TestRun", scenario)
            row.pop("trace", None)
            results.append({"optimization": optimization, "id": scenario, **row})
            print(results[-1], flush=True)

    (output / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    return any(not row["pass"] for row in results)


if __name__ == "__main__":
    raise SystemExit(main())
