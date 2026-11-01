"""Run the preserved 621 executions and the 09C suite, without changing sources."""
from pathlib import Path
import json
import os
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8")
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SUITES = [
    ("Streaming", "Tests/UvcStreaming/run_tests.py", "Tests/UvcStreaming/results.json", 36),
    ("Payload", "Tests/UvcPayload/run_tests.py", "Tests/UvcPayload/results.json", 124),
    ("EP1", "Tests/UvcEp1Lifecycle/run_tests.py", "Tests/UvcEp1Lifecycle/results.json", 45),
    ("Probe", "Tests/UvcProbeCommit/run_tests.py", "Tests/UvcProbeCommit/results.json", 89),
    ("Cancel", "Tests/UvcProbeCommit/run_cancellation_tests.py", "Tests/UvcProbeCommit/cancellation_results.json", 39),
    ("Core", "Tests/UsbCoreDecoupling/run_tests.py", "Tests/UsbCoreDecoupling/results.json", 204),
    ("EP0 IRQ", "Tests/Ep0IrqRace/run_race.py", "Tests/Ep0IrqRace/results.json", 4),
    ("Lifecycle", "Tests/UvcLifecycle/run_tests.py", "Tests/UvcLifecycle/results.json", 40),
    ("Descriptors", "Tests/UsbUvcDescriptors/run_tests.py", "Tests/UsbUvcDescriptors/results.json", 40),
    ("09C", "Tests/UvcTestPattern/run_tests.py", "Tests/UvcTestPattern/results.json", 24),
]
env = dict(os.environ, PYTHONIOENCODING="utf-8", PYTHONPATH="D:/32/STM32H7/h7_mac/Tests/OledAsyncAcceptance/python_deps")
rows = []
for name, script, result_file, expected in SUITES:
    target = ROOT / result_file
    target.unlink(missing_ok=True)
    command = [sys.executable, script]
    p = subprocess.run(command, cwd=ROOT, env=env, capture_output=True)
    output = (p.stdout + p.stderr).decode("utf-8", errors="replace")
    actual = json.loads(target.read_text(encoding="utf-8")) if target.exists() else {}
    if isinstance(actual, list):
        total = len(actual); passed = sum(bool(x.get("pass")) for x in actual)
        summary = dict(total=total, passed=passed, failed=total-passed)
    else:
        summary = actual.get("summary", {})
    total = summary.get("total", summary.get("executions", 0))
    passed = summary.get("passed", 0)
    success = p.returncode == 0 and total == expected and passed == expected
    rows.append(dict(suite=name, command=subprocess.list2cmdline(command), returncode=p.returncode,
        summary=summary, expected_executions=expected, pass_=success, output=output))
    (HERE / "regression_results.json").write_text(json.dumps(dict(suites=rows, passed=sum(x["summary"].get("passed",0) for x in rows),
        expected_executions=sum(x[3] for x in SUITES), all_passed=len(rows)==len(SUITES) and all(x["pass_"] for x in rows)),ensure_ascii=False,indent=2),encoding="utf-8")
    print(name, "PASS" if success else "FAIL", summary, flush=True)
    if not success:
        print(output[-10000:]); raise SystemExit(1)
print("Original 621 + 09C 24 = 645/645 PASS", flush=True)
