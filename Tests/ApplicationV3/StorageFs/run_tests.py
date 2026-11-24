"""Run ARMCC5 StorageFS, FatFs and AVI with an emulated sector device."""

import argparse
import importlib.util
import json
from pathlib import Path
import sys

from elftools.elf.elffile import ELFFile


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
spec = importlib.util.spec_from_file_location("media", HERE.parent / "MediaLease/run_tests.py")
media = importlib.util.module_from_spec(spec)
spec.loader.exec_module(media)
media.ENTRY = "StorageFsTest"
media.OBSERVATIONS = ("g_failure", "g_checks")


def build(out, opt, journal):
    stub = out / "stm32h7xx.h"
    stub.write_text(
        "#include <stdint.h>\n"
        "static inline uint32_t __get_IPSR(void) { return 0U; }\n"
        "static inline uint32_t __get_PRIMASK(void) { return 0U; }\n"
        "static inline void Test_DisableIrq(void) { }\n"
        "static inline void __set_PRIMASK(uint32_t mask) { (void)mask; }\n",
        encoding="ascii",
    )
    includes = ("Common/Inc", "CM7/Platform", "CM7/BSP", "CM7/Framework", "CM7/Media/Inc",
                "CM7/Service", "Middlewares/Third_Party/FatFs", "Board/Inc")
    args = ["--cpu", "Cortex-M7", "--c99", "--split_sections", "-" + opt,
            "-D__disable_irq=Test_DisableIrq", "-I", out]
    for include in includes:
        args.extend(("-I", ROOT / include))
    objects = []
    for path in (HERE / "storage_fs_test.c", ROOT / "CM7/Platform/storage_fs.c",
                 ROOT / "CM7/Platform/diskio.c", ROOT / "CM7/Service/mjpeg_avi.c",
                 ROOT / "Middlewares/Third_Party/FatFs/ff.c"):
        obj = out / (path.stem + "-" + opt + ".o")
        media.run([media.ARMCC / "armcc.exe", *args, "-c", path, "-o", obj], out, journal)
        objects.append(obj)
    image = out / (opt + ".axf")
    media.run([media.ARMCC / "armlink.exe", *objects, "--entry", media.ENTRY, "--scatter",
               ROOT / "Tests/CameraStreamState/test.sct", "-o", image], out, journal)
    with image.open("rb") as binary:
        elf = ELFFile(binary)
        symbols = {symbol.name: symbol["st_value"]
                   for symbol in elf.get_section_by_name(".symtab").iter_symbols()}
        sections = [(section["sh_addr"], section.data()) for section in elf.iter_sections()
                    if section["sh_flags"] & 2 and section["sh_type"] == "SHT_PROGBITS"]
    return symbols, sections


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    out = parser.parse_args().output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    journal, results = [], []
    for opt in ("O0", "O2"):
        image = build(out, opt, journal)
        for case in range(2):
            row = {"optimization": opt, "case": case, **media.execute(image, case)}
            results.append(row)
            print(row, flush=True)
    (out / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    return any(not row["pass"] for row in results)


if __name__ == "__main__":
    sys.exit(main())
