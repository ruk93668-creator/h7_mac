"""Build the ApplicationV3 Ethernet production profile. Never download or flash."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess
import time
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
UV4 = Path("D:/32/UV4/UV4.exe")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--prepare-only", action="store_true")
    parser.add_argument("--output", type=Path, default=ROOT / "output/Ethernet/production")
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    original = ROOT / "MDK-ARM/h7_mac.uvprojx"
    tree = ET.fromstring(original.read_bytes())
    targets = tree.find("Targets")
    target = next(t for t in targets if t.findtext("TargetName") == "h7_mac_CM7")
    for item in list(targets):
        if item is not target:
            targets.remove(item)
    controls = target.find("./TargetOption/TargetArmAds/Cads/VariousControls")
    defines = [v for v in controls.findtext("Define", "").split(",")
               if not v.startswith(("APP_NETWORK_BOOT=", "APP_VIDEO_MJPEG_MODE="))]
    defines += ["APP_NETWORK_BOOT=1", "APP_VIDEO_MJPEG_MODE=1"]
    controls.find("Define").text = ",".join(defines)
    common = target.find("./TargetOption/TargetCommonOption")
    for field in ("OutputDirectory", "ListingPath"):
        common.find(field).text = str(out) + "\\"
    common.find("OutputName").text = "h7_network"
    common.find("CreateHexFile").text = "1"
    paths = [f.findtext("FilePath", "").replace("\\", "/") for f in target.findall(".//File")]
    if any("/Tests/" in path or "fixture" in path.lower() for path in paths):
        raise SystemExit("Production profile contains a test source")
    project = ROOT / "MDK-ARM/h7_mac_ethernet_production.uvprojx"
    ET.ElementTree(tree).write(project, encoding="utf-8", xml_declaration=True)
    if args.prepare_only:
        print(project)
        return 0

    # The isolated repository's generated RTE is restored byte-for-byte.
    rte = ROOT / "MDK-ARM/RTE/_h7_mac_CM7/RTE_Components.h"
    rte_before = rte.read_bytes()
    input_files = []
    for directory in ("Platform/Ethernet", "CM7", "Common", "ThirdParty/lwip"):
        input_files.extend(p for p in (ROOT / directory).rglob("*")
                           if p.suffix in (".c", ".h", ".s", ".sct"))
    input_files += [original, ROOT / "MDK-ARM/h7_mac_CM7_custom.sct", Path(__file__)]
    before = {p.relative_to(ROOT).as_posix(): sha(p) for p in input_files}
    git = ["git", "-c", "safe.directory=" + ROOT.as_posix()]
    commit = subprocess.check_output(git + ["rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    log = out / "build.log"
    started = time.time()
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    try:
        result = subprocess.run([str(UV4), "-j0", "-r", str(project), "-t", "h7_mac_CM7",
                                 "-o", str(log)], cwd=project.parent, startupinfo=startup,
                                creationflags=subprocess.CREATE_NO_WINDOW, timeout=600)
    finally:
        if rte.read_bytes() != rte_before:
            rte.write_bytes(rte_before)
    text = log.read_text(errors="replace") if log.exists() else ""
    match = re.search(r"(\d+) Error\(s\), (\d+) Warning\(s\)", text)
    files = {kind: out / ("h7_network." + kind) for kind in ("axf", "hex", "map")}
    fresh = all(p.is_file() and p.stat().st_mtime >= started - 1 for p in files.values())
    mapping = files["map"].read_text(errors="replace") if fresh else ""
    regions = {}
    for name, address, maximum in (("RW_ETHERNET_DMA", 0x24078000, 0x8000),
                                    ("RW_ETHERNET_CPU", 0x24010000, 0x10000),
                                    ("RW_NETWORK_JPEG", 0xD07F0000, 0xF000),
                                    ("RW_IRAM1", 0x20000000, 0x20000)):
        found = re.search(r"Execution Region " + name +
                          r" \(Exec base: (0x[\da-f]+).*?Size: (0x[\da-f]+), Max: (0x[\da-f]+)",
                          mapping, re.I)
        regions[name] = {"base": int(found[1], 16), "size": int(found[2], 16),
                         "max": int(found[3], 16)} if found else None
        if not found or int(found[1], 16) != address or int(found[3], 16) != maximum:
            fresh = False
    after = {p.relative_to(ROOT).as_posix(): sha(p) for p in input_files}
    report = {
        "source_commit": commit, "source_stable": before == after,
        "source_sha256": before, "project_sha256": sha(project),
        "rte_before": hashlib.sha256(rte_before).hexdigest(), "rte_after": sha(rte),
        "exit": result.returncode, "errors": int(match[1]) if match else None,
        "warnings": int(match[2]) if match else None, "fresh_layout": fresh,
        "regions": regions, "artifacts": {k: {"path": str(p), "sha256": sha(p)}
                                        for k, p in files.items() if p.is_file()},
        "hardware": "NOT_RUN"
    }
    (out / "manifest.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({k: report[k] for k in ("source_commit", "exit", "errors", "warnings",
                                            "source_stable", "fresh_layout")}))
    return int(result.returncode != 0 or not match or match.groups() != ("0", "0") or
               not fresh or before != after or report["rte_before"] != report["rte_after"])


if __name__ == "__main__":
    raise SystemExit(main())

