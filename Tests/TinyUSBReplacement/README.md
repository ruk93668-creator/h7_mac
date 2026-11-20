# TinyUSB replacement experiments — BLOCKED, not flashable firmware

Pinned TinyUSB da5b55a5803a3b9ca788ccc82569db699c01aa74, source manifest in ThirdParty/tinyusb/UPSTREAM.json. No old UDLM, ST Core, HAL PCD or old BOT is linked into the new ARM test images. Existing production Keil targets are untouched and still use their original stack.

Run from the lifecycle worktree:

```powershell
python Tests/TinyUSBReplacement/audit_upstream.py
python Tests/TinyUSBReplacement/run_stack_tests.py
python Tests/TinyUSBReplacement/run_dwc2_tests.py
python Tests/TinyUSBReplacement/run_video_tests.py
python Tests/TinyUSBReplacement/compile_probe.py
```

The first audit uses retained read-only upstream metadata under ignored output/TinyUSBReplacement/upstream. Download that pinned commit from GitHub codeload and its commit/tree API records if reproducing on a fresh machine; verify hashes before using it. ARM runners use existing ARMCC5 and Python Unicorn/pyelftools under output/UDLM-P1/python_deps, never connect to hardware. No packages are vendored as runtime dependencies.

12 stack cases execute real TinyUSB Core/MSC and the existing RAM backend with an explicit fake DCD and OS_NONE. That validates protocol software only. 8 actual DWC2 cases are failure/ordering witnesses, including polling which DOES NOT RETURN under missing hardware facts. 3 Video cases seed an already-owned-frame state to witness Reset/DeInit/abort callback and ABI behavior; they do not configure a video stream. None is a Windows/SD/ISO hardware acceptance.

compile_probe is compile-only for three source profiles using the real project FreeRTOS headers; it intentionally returns failure while warnings/errors remain. It must not be reported as three full firmware builds. Read the final report before any further integration.

The RAM callback adapter always defers completion until TinyUSB_RamPoll in the same USB task. TinyUSB_RamDrained is a backend-only necessary condition, not permission to stop the USB controller. Do not deinit/reinit while !TinyUSB_RamDrained; no production task binding, hardware-safe stop, queue overflow or cross-session retirement qualification is provided. No SD callback is connected. The new config/adapter/descriptors are experiments, not a flash-ready BSP.

All generated binaries/downloads are ignored. Historical P1 reports and all old tests remain unchanged. No physical device operation is performed by these commands.
