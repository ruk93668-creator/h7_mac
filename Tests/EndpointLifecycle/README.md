# UDLM P1 evidence tests (partial / blocked)

These tools characterize the reviewed baseline and real unchanged HAL/LL. They do not implement or certify a new endpoint stop contract. No silicon was programmed or tested.

- `run_baseline.py`: copies tracked inputs into a fresh disposable snapshot under the already ignored `output/MSC24/udlm-p1/`; runs all 17 original suite entry points unchanged. Refuses to overwrite earlier evidence. Requires unchanged baseline inputs from 9d38e56; later docs/evidence-only descendant commits are allowed. `--build --tag unique-tag` runs the existing three-mode isolated Keil builder. Original reports/assertions/project files stay unchanged.
- `run_hal_contract.py`: compiles the actual HAL/LL with ARMCC5 and runs 18 MMIO-injected characterization cases in Unicorn. The test-only `HAL_USB_TIMEOUT=4U` makes timeout injection practical; production timeout is unchanged. Four link-only dependencies are guarded as forbidden invocations. Every case asserts none was called. Output is `reports/hal_contract.json`.
- `hal_contract_test.c` and `test.sct`: isolated unit entry and unit link layout, not production linker changes.
- `reports/`: this run's logs/results/commands/hashes. Baseline failures are retained. The first build snapshot omitted Board and failed; that tool failure is retained separately, followed by successful complete-snapshot builds.

Dependencies: ARMCC5 at `D:/32/ARM/ARMCC/Bin`, Keil UV4 at `D:/32/UV4/UV4.exe`, Python, Unicorn 2.1.4, pyelftools 0.33. This run installed Python dependencies only into ignored `output/UDLM-P1/python_deps`.

```powershell
Set-Location D:/32/STM32H7/h7_mac_lifecycle
python -m pip install --target output/UDLM-P1/python_deps unicorn==2.1.4 pyelftools==0.33
python -u Tests/EndpointLifecycle/run_hal_contract.py
python -u Tests/EndpointLifecycle/run_baseline.py --tag review1
python -u Tests/EndpointLifecycle/run_baseline.py --build --tag review1
```

Characterization PASS means an independently asserted source behavior was reproduced. Several reproduced behaviors are precisely the known unsafe proof gaps. Do not count these PASS results as P1 endpoint lifecycle acceptance. Race/ledger/reconcile acceptance needs an implemented and hardware-supported primitive; see the three EndpointLifecycle documents in Docs/usb-lifecycle.

Tracked `.log` files are normalized UTF-8/LF review copies. Original bytes remain under ignored `output/MSC24/udlm-p1/raw-text-evidence`; raw-log-manifest.json binds both hashes.
