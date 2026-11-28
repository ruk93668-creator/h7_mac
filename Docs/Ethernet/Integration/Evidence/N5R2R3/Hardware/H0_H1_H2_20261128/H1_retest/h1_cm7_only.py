
"""H1 CM7-only programmer. With no arguments it performs an offline preflight."""
from argparse import ArgumentParser
from datetime import datetime
from hashlib import sha256
from io import BytesIO, StringIO
import json
import logging
from pathlib import Path
import sys
from zipfile import ZipFile

from intelhex import IntelHex
from pyocd.core.helpers import ConnectHelper
from pyocd.core.session import Session
from pyocd.flash.file_programmer import FileProgrammer
from pyocd.probe.cmsis_dap_probe import CMSISDAPProbe
from pyocd.target.pack.cmsis_pack import CmsisPack
from pyocd.target.pack.flash_algo import PackFlashAlgo

ROOT = Path(__file__).resolve().parent
HEX = ROOT.parent / "firmware" / "h7_network_acceptance.hex"
PACK = Path("D:/32/.Download/Keil.STM32H7xx_DFP.4.1.3.pack")
HEX_SHA256 = "a5201251f69132b6ac06880cae042e867cb05dbca1126593e4e3d6dc811481e0"
PACK_SHA256 = "42a2358e0cb7344a9a54ee14c95eb17af20ccbe44bd2b0a08b24fefbfb67b8c8"
TARGET = "stm32h747xihx"
FIRST = 0x08000000
END = 0x08035388
BANK2 = 0x08100000
SECTOR = 0x20000


def preflight():
    raw = HEX.read_bytes()
    if sha256(raw).hexdigest() != HEX_SHA256:
        raise ValueError("H0 HEX SHA-256 mismatch. No probe opened.")
    text = raw.decode("ascii")
    hex_data = IntelHex(StringIO(text))
    if hex_data.segments() != [(FIRST, END)]:
        raise ValueError("HEX address range does not match the H0 CM7-only image.")
    expected = bytes(hex_data.tobinarray(start=FIRST, size=END - FIRST))
    # Parse the same locally installed pack bytes that will be used for the probe session.
    pack_data = PACK.read_bytes()
    if sha256(pack_data).hexdigest() != PACK_SHA256:
        raise ValueError("Keil DFP SHA-256 mismatch. No probe opened.")
    pack = CmsisPack(BytesIO(pack_data))
    device, = (d for d in pack.devices if d.part_number.lower() == TARGET)
    bank = device.memory_map.get_region_for_address(FIRST, "CM7")
    with ZipFile(BytesIO(pack_data)) as archive:
        algorithm = PackFlashAlgo(BytesIO(archive.read("CMSIS/Flash/STM32H7x_2048.FLM")))
    if (bank is None or not bank.is_flash or bank.start != FIRST or
            bank.end + 1 != BANK2 or algorithm.sector_sizes != [(0, SECTOR)] or
            ((END - FIRST + SECTOR - 1) // SECTOR) * SECTOR + FIRST != 0x08040000):
        raise ValueError("DFP bank or erase geometry differs from the audited range.")
    return text, expected, pack


def read_range(target, start, length):
    data = bytearray()
    for offset in range(0, length, 32768):
        count = min(32768, length - offset)
        chunk = bytes(target.read_memory_block8(start + offset, count))
        if len(chunk) != count:
            raise IOError(f"Short Flash read at 0x{start + offset:08X}")
        data.extend(chunk)
    return bytes(data)


def verify_flash(expected, actual):
    if len(actual) != len(expected):
        raise IOError("Flash readback length differs from the H0 payload.")
    if expected != actual:
        i = next(i for i, (a, b) in enumerate(zip(expected, actual)) if a != b)
        raise IOError(f"Flash mismatch at 0x{FIRST+i:08X}: read {actual[i]:02X}, expected {expected[i]:02X}")


def program(text, expected, pack, run, report):
    probes = ConnectHelper.get_all_connected_probes(blocking=False)
    if len(probes) != 1 or not isinstance(probes[0], CMSISDAPProbe):
        raise RuntimeError("Expected exactly one CMSIS-DAP probe. Close any active Keil debugging session and check the connection.")
    report["probe"] = probes[0].description
    with Session(probes[0], options={"project_dir": str(run), "target_override": TARGET,
                                    "no_config": True, "pack": pack, "connect_mode": "halt",
                                    "frequency": 1000000, "auto_unlock": False,
                                    "cache.enable_memory": False, "resume_on_disconnect": False}) as session:
        target = session.target
        cpuid = target.read32(0xE000ED00)
        report["cpuid"] = f"0x{cpuid:08X}"
        bank = target.memory_map.get_region_for_address(FIRST)
        if (cpuid >> 4) & 0xFFF != 0xC27 or bank is None or not bank.is_flash or bank.end + 1 != BANK2:
            raise RuntimeError("The selected core or Flash bank does not match the CM7 acceptance target.")
        # Snapshot the sector that the previous mixed Keil run may have programmed.
        cm4_before = read_range(target, BANK2, SECTOR)
        report["cm4_first_sector_sha256_before"] = sha256(cm4_before).hexdigest()
        report["phase"] = "PROGRAM"
        print("Programming approved CM7 HEX with sector erase.", flush=True)
        programmer = FileProgrammer(session, chip_erase="sector", smart_flash=False,
                                    trust_crc=False, keep_unwritten=True)
        programmer.program(StringIO(text), file_format="hex")
        report["phase"] = "VERIFY"
        actual = read_range(target, FIRST, len(expected))
        (run / "cm7_flash_readback.bin").write_bytes(actual)
        report["cm7_readback_sha256"] = sha256(actual).hexdigest()
        verify_flash(expected, actual)
        cm4_after = read_range(target, BANK2, SECTOR)
        report["cm4_first_sector_sha256_after"] = sha256(cm4_after).hexdigest()
        if cm4_before != cm4_after:
            raise IOError("CM4 first sector changed during the attempt. Preserve the log.")
        report["payload_bytes_verified"] = len(actual)


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--program", action="store_true", help="program and verify on the connected board")
    args = parser.parse_args()
    if not args.program:
        text, expected, pack = preflight()
        print(f"OFFLINE_PREFLIGHT_OK {len(expected)} bytes 0x{FIRST:08X}-0x{END-1:08X}")
        return 0

    run = ROOT / ("retry_" + datetime.now().astimezone().strftime("%Y%m%d_%H%M%S_%f"))
    run.mkdir()
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s",
        handlers=[logging.FileHandler(run / "H1_flash_programmer.log", encoding="utf-8"), logging.StreamHandler()],
    )
    result = {"scope": "CM7_FLASH_VERIFY", "status": "FAIL", "phase": "PREFLIGHT", "hex": str(HEX),
              "hex_sha256": HEX_SHA256, "pack_sha256": PACK_SHA256,
              "cm7_range": ["0x08000000", "0x08035387"], "erase": "sector"}
    print(f"Evidence directory: {run}", flush=True)
    try:
        text, expected, pack = preflight()
        result["expected_payload_sha256"] = sha256(expected).hexdigest()
        result["phase"] = "CONNECT"
        program(text, expected, pack, run, result)
        result.update(status="PASS", phase="COMPLETE")
        logging.info("VERIFY_OK: %d bytes matched the approved HEX. CM4 first sector unchanged.", len(expected))
        return 0
    except Exception as exc:
        result["error"] = f"{type(exc).__name__}: {exc}"
        logging.exception("H1 failed at %s", result["phase"])
        return 1
    finally:
        (run / "H1_program_verify.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    sys.exit(main())

