# Timer Framework phase 2 isolated acceptance

CM7 actual source; ARMCC 5.06; Unicorn ARM Thumb M-class; controlled mock tick; no STM32 peripherals

| Test | Input | Expected | Actual | Result |
|---|---|---|---|---|
| One-Shot order | A=300, B=100, C=200 ms; start at 0 | B@100, C@200, A@300; no later repeats | 2@100, 3@200, 1@300; pool/heap=3/0; invariants=18 | Pass |
| Delete root | Periods 700,100,600,200,500,300,400; delete heap[0] at 50 | Remaining callbacks sorted by deadline; pool/heap 6/0 at finish | 4@200, 6@300, 7@400, 5@500, 3@600, 1@700; pool/heap=6/0; invariants=30 | Pass |
| Delete middle | Same seven timers; delete heap[2] at 50 | Remaining callbacks sorted by deadline; pool/heap 6/0 at finish | 2@100, 4@200, 7@400, 5@500, 3@600, 1@700; pool/heap=6/0; invariants=30 | Pass |
| Delete tail | Same seven timers; delete heap[size-1] at 50 | Remaining callbacks sorted by deadline; pool/heap 6/0 at finish | 2@100, 4@200, 6@300, 5@500, 3@600, 1@700; pool/heap=6/0; invariants=30 | Pass |
| Active repeated Start | A=100,B=150; restart A at each tick 1..50 | Heap stays 2; no callback before 150; exactly two at 150 | 1@150, 2@150; pool/heap=2/0; invariants=62 | Pass |
| One-Shot callback Stop | A=100,B=150; A callback stops itself | A once at 100, B once at 150; heap=0, pool=2 | 1@100, 2@150; pool/heap=2/0; invariants=14 | Pass |
| One-Shot callback Start | A=100,B=150; first A callback restarts itself | A@100,B@150,A@200; heap=0, pool=2 | 1@100, 2@150, 1@200; pool/heap=2/0; invariants=15 | Pass |
| One-Shot callback Delete | A=100,B=150; A callback deletes itself | A@100,B@150; old handle rejected; heap=0, pool=1 | 1@100, 2@150; pool/heap=1/0; invariants=14 | Pass |
| Periodic callback Stop | A periodic=100,B one-shot=150; A callback stops itself | A@100,B@150; heap=0, pool=2 | 1@100, 2@150; pool/heap=2/0; invariants=14 | Pass |
| Periodic callback Start | A periodic=100,B one-shot=150; first A callback restarts itself | A@100,B@150,A@200; one active A; heap=1, pool=2 | 1@100, 2@150, 1@200; pool/heap=2/1; invariants=15 | Pass |
| Periodic callback Delete | A periodic=100,B one-shot=150; A callback deletes itself | A@100,B@150; old handle rejected; heap=0, pool=1 | 1@100, 2@150; pool/heap=1/0; invariants=14 | Pass |
| Generation stale handle | Create/start/delete A; recreate B in same slot; use old handle | Generation 1->2; old Start/Stop/Delete rejected; new B@200 | 2@200; pool/heap=1/0; invariants=13; handle 0x00000100->0x00000200 | Pass |
| Tick rollover | Start at 0xFFFFFF80; A=300,B=100,C=200 | B@FFFFFFE4,C@00000048,A@000000AC; no early callbacks | 2@4294967268, 3@72, 1@172; pool/heap=3/0; invariants=19 | Pass |
| Capacity and lifecycle | Create/start 16; stop evens; delete odds; refill; expire all; delete all | 17th create rejected; counts 16/0->16/16->16/8->8/0->16/16->16/0->0/0 | 1@100, 2@101, 3@102, 4@103, 5@104, 6@105, 7@106, 8@107, 9@108, 10@109, 11@110, 12@111, 13@112, 14@113, 15@114, 16@115; pool/heap=0/0; invariants=123 | Pass |

Production source/config hashes unchanged during execution: True
CM7 framework_timer.c SHA256: 21613657f723bc89efb9ae0a7509da8cdbf2e7cb14b17418385e3704734d94e9

All public operations and callback state changes check: allocated count, active count, heap bounds,
unique heap entries, parent/child deadline order with an independent signed-difference oracle,
heap-to-pool and pool-to-heap indices, and stopped/previously allocated slots' invalid heap index.
Each case uses a fresh zero-initialized ARM emulator. Actual source is included unchanged so private
state can be inspected without adding production exports or test hooks.

This runner executes CM7 only; this report does not claim CM4 timer acceptance.
Simulated ticks test protocol logic, not wall-clock timing accuracy on a physical board.
