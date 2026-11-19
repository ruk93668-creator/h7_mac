"""Run existing assertions/builds in a disposable snapshot; never overwrite historical evidence."""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess, sys, time

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "output/MSC24/udlm-p1"
REPORT = ROOT / "Tests/EndpointLifecycle/reports"
BASE = "9d38e56966f8ea5da9ee63285b037747613c47d5"
GIT = ["git", "-c", "safe.directory=" + ROOT.as_posix()]
SUITES = [
    "UsbCoreDecoupling/run_tests.py", "UvcLifecycle/run_tests.py",
    "UvcStreaming/run_tests.py", "UvcPayload/run_tests.py",
    "UvcEp1Lifecycle/run_tests.py", "UvcProbeCommit/run_tests.py",
    "UvcProbeCommit/run_cancellation_tests.py", "Ep0IrqRace/run_race.py",
    "UsbUvcDescriptors/run_tests.py", "UvcUsbCleanup/run_tests.py",
    "MscDeferredStatus/run_tests.py", "MscProtocol/run_tests.py",
    "MscBlockAdapter/run_tests.py", "MscSdIntegration/run_tests.py",
    "MscEnumeration/run_tests.py", "MscEnumeration/run_integration.py",
    "MscSenseCompat/run_tests.py",
]

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", action="store_true")
    parser.add_argument("--tag", default="")
    args = parser.parse_args()
    head = subprocess.check_output(GIT + ["rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    subprocess.run(GIT + ["merge-base", "--is-ancestor", BASE, head], cwd=ROOT, check=True)
    changes = subprocess.check_output(GIT + ["diff", "--name-only", BASE, "--", "CM7", "CM4", "Board",
                  "Common", "Drivers", "Middlewares", "MDK-ARM", "Tests", ":(exclude)Tests/EndpointLifecycle"],
                  cwd=ROOT, text=True).strip()
    if changes:
        raise RuntimeError("Baseline input differs from reviewed revision: " + changes)
    label = "baseline-build" if args.build else "baseline-regression"
    if args.tag:
        if not args.tag.replace("-", "").isalnum():
            raise ValueError("Invalid tag")
        label += "-" + args.tag
    work = OUT / label
    if work.exists():
        raise RuntimeError("Refusing to overwrite an earlier evidence directory: " + str(work))
    work.mkdir(parents=True)
    tracked = subprocess.check_output(GIT + ["ls-files", "-z"], cwd=ROOT).decode().split("\0")
    manifest = {}
    for name in tracked:
        if not name or name.split("/")[0] not in ("CM7", "CM4", "Board", "Common", "Drivers", "Middlewares", "MDK-ARM", "Tests"):
            continue
        source = ROOT / name
        if not source.is_file():
            continue
        dest = work / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, dest)
        manifest[name] = digest(source)
    env = dict(os.environ, PYTHONPATH=str(ROOT / "output/UDLM-P1/python_deps"),
               PYTHONDONTWRITEBYTECODE="1", PYTHONIOENCODING="utf-8")
    rows = []
    suites = ["MscReconnect/build.py"] if args.build else SUITES
    REPORT.mkdir(parents=True, exist_ok=True)
    for suite in suites:
        start = time.monotonic()
        result = subprocess.run([sys.executable, str(work / "Tests" / suite)], cwd=work,
                                env=env, capture_output=True)
        log = (result.stdout + result.stderr).decode("utf-8", errors="replace")
        logpath = REPORT / (label + "_" + suite.replace("/", "_").replace(".py", ".log"))
        rawpath = OUT / "raw-text-evidence" / logpath.name
        rawpath.parent.mkdir(parents=True, exist_ok=True)
        rawpath.write_bytes(result.stdout + result.stderr)
        log = "\n".join(x.rstrip() for x in log.splitlines()).rstrip() + "\n"
        logpath.write_bytes(log.encode("utf-8"))
        # These are outputs of the unchanged suites in the snapshot, not imported historical pass claims.
        emitted = []
        for path in (work / "Tests" / suite.split("/")[0]).glob("*.json"):
            relative = path.relative_to(work).as_posix()
            if relative not in manifest or digest(path) != manifest[relative]:
                emitted.append({"path": relative, "sha256": digest(path),
                                "content": json.loads(path.read_text(encoding="utf-8"))})
        rows.append(dict(suite=suite, exit=result.returncode, elapsed_s=round(time.monotonic()-start, 3),
                         log=str(logpath.relative_to(ROOT)), log_sha256=digest(logpath),
                         raw_log_path=str(rawpath.relative_to(ROOT)), raw_log_sha256=digest(rawpath),
                         produced_reports=emitted, tail=log[-1600:]))
        print(label, suite, "EXIT", result.returncode, log[-240:].replace("\n", " "), flush=True)
        report = dict(baseline=BASE, production_modified=False, unchanged_assertions=True,
                      scope="software mocks/build only; no silicon proof or flashing", rows=rows,
                      original_changed=[n for n,h in manifest.items() if digest(ROOT/n) != h])
        (REPORT / (label + ".json")).write_bytes(json.dumps(report, ensure_ascii=False, indent=2).encode("utf-8"))
    if report["original_changed"]:
        raise RuntimeError("Source changed while validating baseline")
    print("FINISHED", label, "suites", len(rows), "failed", sum(x["exit"] != 0 for x in rows), flush=True)

if __name__ == "__main__":
    main()
