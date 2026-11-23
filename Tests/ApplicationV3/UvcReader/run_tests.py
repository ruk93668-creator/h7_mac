"""Run A3 Class/Payload/USB Platform tests with the original Cortex-M USB harness."""

import argparse
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    directory = parser.parse_args().output.resolve()
    directory.mkdir(parents=True, exist_ok=False)
    original = (ROOT / "Tests/CameraMjpegStage03B/run_usb.py").read_text(encoding="utf-8")
    failure = False
    for optimization in ("O0", "O2"):
        out = directory / optimization
        out.mkdir()
        driver = original.replace(
            "ROOT=Path(__file__).resolve().parents[2]; HERE=Path(__file__).resolve().parent",
            f"ROOT=Path({str(ROOT)!r}); HERE=Path({str(out)!r})",
        ).replace("'-O0'", repr("-" + optimization)).replace(
            "HERE/(args.name+'.c')",
            f"Path({str(HERE / 'reader_test.c')!r}), ROOT/'CM7/ApplicationV3/Core/video_config.c', ROOT/'CM7/Platform/platform_sync.c'",
        ).replace(
            "'-DSTM32H747xx'", "'-DSTM32H747xx','-DAPP_V3_RAW_UVC_TEST=1'"
        ).replace(
            "'CM7/Core/Inc'", "'CM7/Core/Inc','CM7/ApplicationV3/Core'"
        ).replace("count=3000000", "count=12000000")
        path = out / "driver.py"
        path.write_text(driver, encoding="utf-8")
        result = subprocess.run([sys.executable, path, "reader", "--count", "11"], cwd=ROOT)
        failure |= bool(result.returncode)
    return failure


if __name__ == "__main__":
    sys.exit(main())
