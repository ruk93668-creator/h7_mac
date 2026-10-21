# Return-code migration regression

Run from PowerShell with Python dependencies `unicorn` and `pyelftools` available
in the directory supplied to `-PythonDeps`:

```powershell
& 'D:\32\STM32H7\h7_mac\Tests\ResultMigration\run_result_test.ps1' -PythonDeps '<dependency-directory>'
```

Uses the actual CM7/CM4 Initcall, Event, Timer and IPC Service source files;
CM7 also tests the existing unimplemented Device_Register return paths.
Time, memory barrier and ARM C runtime helpers are isolated test stubs.
Each test starts in a fresh zero-initialized emulator.

Tests verify successful level ordering and first-failure early return, invalid
levels, every result constant, every IPC fault bit, fault accumulation, the exact
shared structure size/offsets/version, event invalid/duplicate/full returns,
timer invalid/stale/full returns, and Device invalid/unsupported returns.
The test scatter file is separate from the unchanged production scatter files.

Actual Keil rebuild logs are `h7_mac_CM7_final.log` and `h7_mac_CM4_final.log`.
Both targets were rebuilt using `D:\32\UV4\UV4.exe -r` with ARMCC 5.06 update 7.
