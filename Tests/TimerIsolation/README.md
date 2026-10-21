# Timer Framework isolated phase 2 acceptance

Only this directory is added. The Keil project, public headers and production
framework are not edited. The test translation unit includes the actual CM7
source to inspect private pool/heap state without modifying it. Each case runs
in a fresh emulator; Platform_TimeGetMs is a controlled test stub. No HAL,
SysTick, HSEM or physical STM32 peripheral is linked.

Requirements: ARMCC 5.06 tools, Python, `unicorn` and `pyelftools`.
Dependencies may be installed to an external temporary directory using
`python -m pip install unicorn pyelftools --target <temporary-directory>`.

From the project root, run:

```powershell
& .\Tests\TimerIsolation\run_phase2.ps1 -PythonDeps '<temporary-directory>'
```

If dependencies are already importable, omit `-PythonDeps`.
Object/AXF files are placed in a fresh temporary directory by default.
Actual compile/link commands, logs and individual input/expected/actual/result
rows are generated in this test directory. No Keil Target is rebuilt.

The 14 cases include both one-shot and periodic callback self-operations,
deletions at actual heap positions (not pool positions), stale handles, rollover,
and full-pool/stop/delete/refill/expiration lifecycles. Every state-changing
operation checks heap ordering and both directions of the pool/heap indices.
The ordering oracle uses signed subtraction independently of the implementation's
unsigned comparison and assumes compared intervals below 2^31 ms.

Result scope is CM7; this runner does not execute the CM4 timer implementation.
Failures are reported by test source assertion line; production code is never
patched by the runner. Simulated tick timing does not certify physical-board
wall-clock latency.
