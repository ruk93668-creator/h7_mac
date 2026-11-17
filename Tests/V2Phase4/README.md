# Phase4 Camera software acceptance

Run run_tests.py, run_negative.py, run_execution.py, run_resource.py,
run_regression.py, run_protocol.py and run_build.py with Python from this repo.
Existing ARMCC5/UV4, Unicorn2.1.4 and pyelftools0.33 dependencies are used.
77 software IDs run once per O0/O2. Real OV5640 and Capture validators are linked;
hardware dependencies trap immediately if accidentally executed. No driver
validator is stubbed. Native control, resource, and queue actors are controlled
fixtures; no real RTOS concurrency/hardware acceptance is claimed.

TestMatrix/ScenarioMapping preserve original 72/32 integration gates as NOT_RUN.
The Legacy proposal is documentation only. Original assertions, source and Keil
settings remain unchanged. Actual test/AXF/MAP/HTM hashes and raw logs are saved;
binaries stay in output/V2Phase4, not Git. Two NOT_PROVEN remain.
