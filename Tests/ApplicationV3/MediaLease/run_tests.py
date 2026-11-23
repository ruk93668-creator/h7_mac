"""Build A1 with ARMCC5 and run the production Media code on an emulated Cortex-M7."""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_MCLASS, UC_MODE_THUMB
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_SP


ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
ARMCC = Path("D:/32/ARM/ARMCC/Bin")
ENTRY = "A1_TestRun"
SCENARIOS = [f"four-loan release order {rank + 1}" for rank in range(24)] + [
    "full pool, duplicate Acquire and invalid identities",
    "cross-instance ABA and identity exhaustion",
    "rotation failure before address write",
    "rotation failure after address write",
    "legacy isolation and long-held Storage buffer",
    "invalid pool without a safe replacement",
    "unchanged JPEG FIFO and SD retain/release",
]
OBSERVATIONS = ("g_failure", "g_checks", "g_stream_size", "g_capture_size", "g_lease_size")


def run(command, directory, journal):
    command = list(map(str, command))
    completed = subprocess.run(command, cwd=ROOT, capture_output=True)
    record = {
        "command": command,
        "exit": completed.returncode,
        "stdout": completed.stdout.decode("utf-8", errors="replace"),
        "stderr": completed.stderr.decode("utf-8", errors="replace"),
    }
    journal.append(record)
    (directory / "commands.json").write_text(json.dumps(journal, indent=2), encoding="utf-8")
    if completed.returncode:
        raise RuntimeError(record["stdout"] + record["stderr"])


def build(directory, optimization, journal):
    opts = ["--cpu", "Cortex-M7", "--c99", "--split_sections", "-" + optimization]
    for include in (
        ROOT / "Common/Inc",
        ROOT / "CM7/Media/Inc", ROOT / "CM7/BSP", ROOT / "CM7/Platform",
    ):
        opts.extend(("-I", include))
    sources = [HERE / "media_lease_test.c", ROOT / "CM7/Media/Src/camera_mjpeg_fifo.c"]
    objects = []
    for source in sources:
        obj = directory / (source.stem + "-" + optimization + ".o")
        stub = "CameraStreamState" if source == sources[0] else "MjpegSdramFifo"
        # Bind the host-side interrupt stub without shadowing the ARMCC intrinsic name.
        irq_stub = ["-D__disable_irq=Test_FifoDisableIrq"] if stub == "MjpegSdramFifo" else []
        run([ARMCC / "armcc.exe", *opts, *irq_stub, "-I", ROOT / "Tests" / stub / "stubs",
             "-c", source, "-o", obj], directory, journal)
        objects.append(obj)
    image = directory / (optimization + ".axf")
    run([ARMCC / "armlink.exe", *objects, "--entry", ENTRY, "--scatter",
         ROOT / "Tests/CameraStreamState/test.sct", "--list", directory / (optimization + ".map"),
         "--map", "-o", image], directory, journal)
    with image.open("rb") as binary:
        elf = ELFFile(binary)
        symbols = {symbol.name: symbol["st_value"]
                   for symbol in elf.get_section_by_name(".symtab").iter_symbols()}
        sections = [(section["sh_addr"], section.data()) for section in elf.iter_sections()
                    if section["sh_flags"] & 2 and section["sh_type"] == "SHT_PROGBITS"]
    return symbols, sections


def execute(image, scenario):
    symbols, sections = image
    cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    for address, size in ((0x08000000, 0x100000), (0x20000000, 0x40000),
                          (0xD0000000, 0x800000), (0x01000000, 0x1000)):
        cpu.mem_map(address, size)
    for address, image_bytes in sections:
        if image_bytes:
            cpu.mem_write(address, image_bytes)
    cpu.reg_write(UC_ARM_REG_SP, 0x2003FFF0)
    cpu.reg_write(UC_ARM_REG_LR, 0x01000001)
    cpu.reg_write(UC_ARM_REG_R0, scenario)
    error = None
    try:
        cpu.emu_start(symbols[ENTRY] | 1, 0x01000000, count=20000000, timeout=10000000)
    except Exception as exc:
        error = str(exc)

    metrics = {name: struct.unpack("<I", cpu.mem_read(symbols[name], 4))[0] for name in OBSERVATIONS}
    result = cpu.reg_read(UC_ARM_REG_R0)
    passed = error is None and not result and not metrics["g_failure"]
    return {
        "pass": passed and cpu.reg_read(UC_ARM_REG_PC) == 0x01000000,
        "exit": result,
        "error": error,
        **metrics,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    directory = parser.parse_args().output.resolve()
    directory.mkdir(parents=True, exist_ok=False)
    journal, tests = [], []
    run([ARMCC / "armcc.exe", "--vsn"], directory, journal)

    for optimization in ("O0", "O2"):
        image = build(directory, optimization, journal)
        for scenario, name in enumerate(SCENARIOS):
            row = {"optimization": optimization, "id": scenario, "name": name, **execute(image, scenario)}
            tests.append(row)
            print(row, flush=True)

    sources = ["CM7/Media/Inc/camera_capture.h", "CM7/Media/Src/camera_capture.c",
               "CM7/Media/Src/camera_mjpeg_fifo.c", "CM7/BSP/bsp_dcmi.c"]
    payload = {"cases": tests, "hardware": "NOT_RUN", "source_sha256": {
        name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in sources}}
    (directory / "results.json").write_text(json.dumps(payload, indent=2), encoding="utf-8")
    return any(not row["pass"] for row in tests)


if __name__ == "__main__":
    sys.exit(main())
