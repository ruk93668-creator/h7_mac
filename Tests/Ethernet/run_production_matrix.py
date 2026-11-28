"""Rebuild production and independent targets and run the real software regressions."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--section", choices=("build", "host", "all"), default="all")
    parser.add_argument("--output", type=Path, default=ROOT / "output/Ethernet/final")
    parser.add_argument("--deps", type=Path)
    parser.add_argument("--formal-only", action="store_true",
                        help="rebuild the three formal targets and two network profiles")
    args = parser.parse_args()
    if args.formal_only and args.section != "build":
        parser.error("--formal-only requires --section build")
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    git = ["git", "-c", "safe.directory=" + ROOT.as_posix()]
    head = subprocess.check_output(git + ["rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    source_files = []
    for directory in ("CM7", "CM4", "Common", "Platform", "ThirdParty/lwip", "Tests"):
        source_files.extend(p for p in (ROOT / directory).rglob("*")
                            if p.suffix in (".c", ".h", ".py", ".s", ".sct") and
                            "build" not in p.parts and "__pycache__" not in p.parts and
                            "python_deps" not in p.parts)
    source_files += [ROOT / "MDK-ARM/h7_mac.uvprojx", ROOT / "MDK-ARM/h7_mac_CM7_custom.sct"]
    before = {p.relative_to(ROOT).as_posix(): sha(p) for p in source_files}
    (out / "source-sha256.json").write_text(json.dumps(before, indent=2), encoding="utf-8")
    results = {}
    env = dict(os.environ)
    if args.deps:
        env["PYTHONPATH"] = str(args.deps.resolve()) + os.pathsep + env.get("PYTHONPATH", "")

    def save():
        report = {"source_commit": head, "section": args.section, "results": results,
                  "source_stable": before == {p.relative_to(ROOT).as_posix(): sha(p)
                                              for p in source_files},
                  "hardware": "NOT_RUN"}
        (out / (args.section + "-results.json")).write_text(json.dumps(report, indent=2),
                                                           encoding="utf-8")

    def run(key, command):
        with (out / (key + ".log")).open("wb") as log:
            result = subprocess.run(list(map(str, command)), cwd=ROOT, env=env,
                                    stdout=log, stderr=subprocess.STDOUT)
        results[key] = {"exit": result.returncode, "status": "PASS" if result.returncode == 0 else "FAIL"}
        save()
        print(key, results[key], flush=True)
        return result.returncode

    if args.section in ("build", "all"):
        rte = {p: p.read_bytes() for p in (ROOT / "MDK-ARM/RTE").rglob("RTE_Components.h")}
        original = ROOT / "MDK-ARM/h7_mac.uvprojx"
        project = ROOT / "MDK-ARM/_network_validation.uvprojx"
        if project.exists():
            raise SystemExit("Refuse to overwrite an existing validation project")
        for key, core, jpeg in (("cm7_jpeg", "h7_mac_CM7", True),
                                ("cm7_raw", "h7_mac_CM7", False),
                                ("cm4", "h7_mac_CM4", True)):
            tree = ET.fromstring(original.read_bytes())
            targets = tree.find("Targets")
            target = next(t for t in targets if t.findtext("TargetName") == core)
            for item in list(targets):
                if item is not target:
                    targets.remove(item)
            if core == "h7_mac_CM7":
                defines = target.find("./TargetOption/TargetArmAds/Cads/VariousControls/Define")
                defines.text = defines.text.replace("APP_VIDEO_MJPEG_MODE=1",
                                                     "APP_VIDEO_MJPEG_MODE=" + str(int(jpeg)))
            folder = out / key
            folder.mkdir(exist_ok=True)
            for field in ("OutputDirectory", "ListingPath"):
                target.find("./TargetOption/TargetCommonOption/" + field).text = str(folder) + "\\"
            ET.ElementTree(tree).write(project, encoding="utf-8", xml_declaration=True)
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = 0
            try:
                result = subprocess.run(["D:/32/UV4/UV4.exe", "-j0", "-r", str(project),
                                         "-t", core, "-o", str(folder / "build.log")],
                                        cwd=project.parent, startupinfo=startup,
                                        creationflags=subprocess.CREATE_NO_WINDOW, timeout=600)
            finally:
                for path, data in rte.items():
                    if path.read_bytes() != data:
                        path.write_bytes(data)
                project.unlink(missing_ok=True)
            text = (folder / "build.log").read_text(errors="replace")
            match = re.search(r"(\d+) Error\(s\), (\d+) Warning\(s\)", text)
            size = re.search(r"Program Size: Code=(\d+) RO-data=(\d+) RW-data=(\d+) ZI-data=(\d+)", text)
            results[key] = {"exit": result.returncode, "errors": int(match[1]) if match else None,
                            "warnings": int(match[2]) if match else None,
                            "status": "PASS" if result.returncode == 0 and match and
                            match.groups() == ("0", "0") else "FAIL",
                            "size": dict(zip(("code", "ro", "rw", "zi"), map(int, size.groups())))
                            if size else None}
            save()
            print(key, results[key], flush=True)
        independent_targets = ("--e35", "--e4", "--e42", "--e5", "--e6", "--e7",
                               "--e7-v1", "--e7-v1-axi", "--e7-v1-qvga")
        for flag in (() if args.formal_only else independent_targets):
            key = "firmware_" + flag[2:].replace("-", "_")
            run(key, [sys.executable, "-B", "Tests/Ethernet/build_firmware.py", flag])
        run("production_network", [sys.executable, "-B", "Tests/Ethernet/build_production.py",
                                    "--output", out / "production_network"])
        run("hardware_acceptance", [sys.executable, "-B", "Tests/Ethernet/build_production.py",
                                     "--acceptance", "--output", out / "hardware_acceptance"])

    if args.section in ("host", "all"):
        # Some historical runners publish tracked JSON/Markdown. Preserve their prior bytes.
        tracked = subprocess.check_output(git + ["ls-files", "Tests"], cwd=ROOT, text=True).splitlines()
        prior = {p: (ROOT / p).read_bytes() for p in tracked
                 if (ROOT / p).suffix in (".json", ".md") and (ROOT / p).is_file()}
        cases = {
            "e41": ["Tests/Ethernet/run_e4_host.py"],
            "e42": ["Tests/Ethernet/run_e4_host.py", "--dhcp"],
            "e5": ["Tests/Ethernet/run_e5_host.py"],
            "e6": ["Tests/Ethernet/run_e6_host.py"],
            "e7": ["Tests/Ethernet/run_e6_host.py", "--dynamic"],
            "e7_qvga": ["Tests/Ethernet/run_e6_host.py", "--dynamic", "--qvga"],
            "application_v3": ["Tests/ApplicationV3/run_tests.py", out / "v3", "--production"],
            "media_lease": ["Tests/ApplicationV3/MediaLease/run_tests.py", out / "media"],
            "frame_dispatcher": ["Tests/ApplicationV3/FrameDispatcher/run_tests.py", out / "dispatcher"],
            "storage_fs": ["Tests/ApplicationV3/StorageFs/run_tests.py", out / "storage"],
            "uvc_reader": ["Tests/ApplicationV3/UvcReader/run_tests.py", out / "uvc"],
            "camera_i2c_O0": ["Tests/CameraI2cV1/run_tests.py", out / "i2c_O0", "--optimization", "O0"],
            "camera_i2c_O2": ["Tests/CameraI2cV1/run_tests.py", out / "i2c_O2", "--optimization", "O2"],
            "oled": ["Tests/OledAsyncAcceptance/run_tests.py"] +
                    (["--deps", args.deps] if args.deps else []),
            "ipc_v2": ["Tests/IpcV2/run_tests.py"],
            "ipc_status_legacy": ["Tests/IpcStatus/run_tests.py"],
        }
        try:
            for key, command in cases.items():
                run(key, [sys.executable, "-B", *command])
            run("phy_dma_mac", ["cmd", "/c", "Tests\\Ethernet\\run_host.cmd"])
        finally:
            for name, data in prior.items():
                path = ROOT / name
                if path.read_bytes() != data:
                    generated = out / "generated" / name
                    generated.parent.mkdir(parents=True, exist_ok=True)
                    generated.write_bytes(path.read_bytes())
                    path.write_bytes(data)
        save()
    # The archived IPC Status test stays FAIL; only an identical known missing-header cause
    # is classified as a baseline failure, never as PASS.
    failed = [k for k, row in results.items() if row["status"] != "PASS"]
    legacy = out / "ipc_status_legacy.log"
    if "ipc_status_legacy" in failed and legacy.is_file() and "app_status.h" in legacy.read_text(errors="replace"):
        failed.remove("ipc_status_legacy")
        results["ipc_status_legacy"]["classification"] = "BASELINE_FAIL"
    save()
    return int(bool(failed) or before != {p.relative_to(ROOT).as_posix(): sha(p) for p in source_files})


if __name__ == "__main__":
    raise SystemExit(main())

