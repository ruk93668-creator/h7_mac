"""ARMCC5 execution of Camera Dispatcher with real A1 Raw Media and simulated consumers."""

import argparse
import importlib.util
import json
from pathlib import Path
import sys


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
spec = importlib.util.spec_from_file_location("media_harness", HERE.parent / "MediaLease/run_tests.py")
media = importlib.util.module_from_spec(spec)
spec.loader.exec_module(media)
media.ENTRY = "A2_TestRun"


def build(out, optimization, journal):
    options = ["--cpu", "Cortex-M7", "--c99", "--split_sections", "-" + optimization]
    includes = [
        "Common/Inc", "CM7/Media/Inc", "CM7/BSP", "CM7/Platform", "CM7/Device/Inc", "CM7/Framework",
        "CM7/ApplicationV3/Core", "CM7/ApplicationV3/Runtime", "CM7/ApplicationV3/Tasks",
        "CM7/Service", "CM7/Core/Inc", "Board/Inc", "Middlewares/Third_Party/FreeRTOS/Source/include",
        "Middlewares/Third_Party/FreeRTOS/Source/portable/RVDS/ARM_CM7/r0p1",
    ]
    for include in includes:
        options.extend(("-I", ROOT / include))
    sources = (HERE / "dispatch_test.c", ROOT / "CM7/Media/Src/camera_mjpeg_fifo.c")
    objects = []
    for index, source in enumerate(sources):
        stub = "CameraStreamState" if index == 0 else "MjpegSdramFifo"
        seam = [] if index == 0 else ["-D__disable_irq=Test_FifoDisableIrq"]
        obj = out / (source.stem + "-" + optimization + ".o")
        media.run([media.ARMCC / "armcc.exe", *options, *seam, "-I", ROOT / "Tests" / stub / "stubs",
                   "-c", source, "-o", obj], out, journal)
        objects.append(obj)
    image = out / (optimization + ".axf")
    media.run([media.ARMCC / "armlink.exe", *objects, "--entry", media.ENTRY, "--scatter",
               ROOT / "Tests/CameraStreamState/test.sct", "-o", image], out, journal)
    with image.open("rb") as binary:
        elf = media.ELFFile(binary)
        symbols = {symbol.name: symbol["st_value"]
                   for symbol in elf.get_section_by_name(".symtab").iter_symbols()}
        sections = [(section["sh_addr"], section.data()) for section in elf.iter_sections()
                    if section["sh_flags"] & 2 and section["sh_type"] == "SHT_PROGBITS"]
    return symbols, sections


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    out = parser.parse_args().output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    journal, results = [], []
    for optimization in ("O0", "O2"):
        image = build(out, optimization, journal)
        for scenario in range(2):
            row = {"optimization": optimization, "id": scenario, **media.execute(image, scenario)}
            results.append(row)
            print(row, flush=True)
    (out / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    return any(not row["pass"] for row in results)


if __name__ == "__main__":
    sys.exit(main())
