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
    messages = ROOT / "CM7/Application/app_messages.c"
    sources = [Path(__file__).with_name("startup_test.c"), *sorted(V3.rglob("*.c")),
               ROOT / "CM7/Application/app_camera_lifecycle.c"]
    results = []
    for optimization in ("O0", "O2"):
        harness.cc = ["-" + optimization if flag in ("-O0", "-O2") else flag for flag in harness.cc]
        harness.cc += ["--split_sections"]
        name = "app_v3_" + optimization
        harness.build(name, sources, "AppV3_TestRun", [
            "-DAPP_V3_BUILD=1", f'-DC1_MESSAGES_SOURCE="{messages.as_posix()}"'
        ])
        for scenario in range(7):
            row = harness.execute(name, "AppV3_TestRun", scenario)
            row.pop("trace", None)
            results.append({"optimization": optimization, "id": scenario, **row})
            print(results[-1], flush=True)

    (output / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    return any(not row["pass"] for row in results)


if __name__ == "__main__":
    raise SystemExit(main())
