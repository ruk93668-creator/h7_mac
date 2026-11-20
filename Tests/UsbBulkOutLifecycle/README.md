# P1-C1 software tests

These are software/ARM emulation tests, not board acceptance. The initial fixture represents a successfully initialized, disconnected PIO controller; it deliberately does not verify RCC/PHY bring-up. It then executes the real Enable/Start/Open, HAL IRQ, LL RX copy, BSP ledger and Platform functions. No MSC/UVC class migration occurs.

Run in the lifecycle worktree, with ARMCC 5.06u7 and the existing ignored `output/UDLM-P1/python_deps` (Unicorn / pyelftools). All generated firmware stays under ignored output. Never run the historical build helper in the original project; the driver first makes a disposable snapshot.

```powershell
$env:PYTHONDONTWRITEBYTECODE = '1'
$env:PYTHONIOENCODING = 'utf-8'
python Tests/UsbBulkOutLifecycle/run_hook_tests.py
# Require its successful exit before the stop tests.
python Tests/UsbBulkOutLifecycle/run_stop_tests.py
python Tests/UsbBulkOutLifecycle/run_validation.py --in-baseline --tag NEW-UNIQUE-TAG
python Tests/UsbBulkOutLifecycle/run_validation.py --c0 --tag NEW-UNIQUE-TAG
python Tests/UsbBulkOutLifecycle/run_validation.py --tag NEW-UNIQUE-TAG
python Tests/UsbBulkOutLifecycle/run_validation.py --build --tag NEW-UNIQUE-TAG
python Tests/UsbBulkOutLifecycle/compare_results.py --tag NEW-UNIQUE-TAG
python Tests/UsbBulkOutLifecycle/collect_build_evidence.py --tag NEW-UNIQUE-TAG
```

Use a fresh tag: existing output is never removed or overwritten. Freeze tracked source inputs until snapshot runs finish. Original tests and assertions are not edited. The historical 17 entries have known FAILs; the comparison, not an invented all-green summary, is the gate.

`run_hook_tests.py` links actual HAL PCD and LL with independent hooks, without the BSP stop algorithm. `run_stop_tests.py` links actual HAL/LL with BSP/Platform. `harness.py` models destructive FIFO reads, register side effects and bounded delayed events. Commands cause physical register transitions in the model; it never writes a software proof stage, owner flag or reference count to manufacture retirement. Fixture counter-exhaustion operations and initial conditions are explicit fault inputs. The real ARM code alone clears resources.

The model is not an STM32 hardware timing oracle. Missing GONAKEFF, EPDISD or CGONAK completion is injected by holding the corresponding hardware condition; the same code must time out while preserving references. Raw reads/writes, input steps, actual/expected assertions and source hashes are in `reports/hooks.json` and `reports/stop.json`. Earlier reports, including first failures, remain immutable history files.

Final source tag: `c1-final05`. Reports final01/02/03 are superseded intermediate source checks, retained for review. Firmware is not flashed; no writable SD mode is enabled. The original C0 canonical report is unchanged; `c0-intermediate-unbound.json` preserves an earlier invocation and its historical C0 metadata, not a claim that production HAL was unchanged in C1.
