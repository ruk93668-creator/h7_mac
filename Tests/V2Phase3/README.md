# Phase3 Execution software tests

Run python run_tests.py, run_negative.py, run_resource.py, run_regression.py,
run_protocol.py, run_build.py from this repository. ARMCC5/UV4 paths follow the
existing local installation; Unicorn2.1.4 and pyelftools0.33 use the existing
ignored output/V2Phase1/python_deps directory. No package or binary is committed.

TestMatrix.csv enumerates all 79 cases. results.json records 158 O0/O2 executions
and final source hashes; negative_results.json requires real assertion failure,
not compiler/emulator failure. Evidence keeps commands/exit codes and raw traces.
Independent ARM AXF/MAP/HTM and Keil snapshots remain under output/V2Phase3.

Mock actors run serially under explicit owner identities. They simulate queue
backpressure and completion delivery, not FreeRTOS task concurrency. Production
adapters must provide actual serialized owner contexts and durable transport.
No hardware or old BOOT integration is claimed. Two Resource NOT_PROVEN remain.
