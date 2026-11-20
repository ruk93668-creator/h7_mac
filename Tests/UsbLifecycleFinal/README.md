# P1-FINAL evidence and offline hardware preparation

Scope is **partial / BLOCKED**, not a complete P1 acceptance. Production C1 Enable is deliberately not connected. The real ARM dual-endpoint tests end at the missing EP0/consumer-ticket gate and assert that Class/Controller resources remain held. The MSC consumer test uses the real mailbox and always refuses USB Close; it never fabricates an endpoint proof. Gate tests confirm existing unsafe register ordering, not a verified ZLP workaround.

- `run_dual_tests.py`: actual BSP/Platform/HAL/LL, 21 cases; MMIO only for hardware transitions, no software PROVEN/refcount writes.
- `run_worker_tests.py`: actual Class/BOT/Storage DRAIN guard, 3 cases; no SD controller access.
- `run_gate_tests.py`: actual HAL/LL register-order witnesses, 5 checks; physical AHB timing NOT_TESTED.
- `run_validation.py`: disposable snapshots for original 76, C0 24, C1 19/57, 17 legacy entries and three full builds. Use fresh tags, never clean previous output.
- `compare_results.py`: original case/result comparison and all original Tests blob hashes, plus newly missing legacy linker symbols.
- `collect_build_evidence.py`: final AXF/HEX/MAP hashes, logs, source equality and actual ELF/map references. Symbols do not prove production C1 binding.
- `hardware_tools.py`: prepare SAVE-only scripts from ELF, parse saved Intel HEX, analyze *offline* files via `tshark -r`; never flashes, opens debug probes, captures live USB or writes a disk.
- `run_hardware_tool_tests.py`: offline parser/refusal tests and empty-capture tshark pipeline only. Missing evidence stays NOT_TESTED/UNRESOLVED.

Existing ARMCC/Keil and ignored `output/UDLM-P1/python_deps` are reused. Set PYTHONDONTWRITEBYTECODE=1 and PYTHONIOENCODING=utf-8. Unit runners write only their own report paths and ignored output. Preserve histories. Do not run a tracked-input-changing unit runner while validation snapshots are active (the driver detects that change).

Final software tag: `p1-final05` (earlier tags are retained intermediate versions). Stop at the documented LL/EP0 gate. See Docs/usb-lifecycle/P1_Final_ImplementationReport.md for actual completion/blocked state. Do not infer permission for hardware execution from the presence of snapshot scripts or firmware files.
