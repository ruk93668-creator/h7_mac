# Ethernet independent bring-up — work in progress

2026-11-16. Branch `codex/ethernet-bringup`, initial baseline `f0ce45f` (local main).
The sibling h7_mac working tree is read-only input. No merge into main is authorized.
This checkpoint is NOT a production Ethernet driver or a completed delivery.

## Hardware evidence

Source: user-provided SVG_Schematic1_1_2026-11-16.zip, sheets 2 and 5,
visually inspected after SVG rendering. Schematic evidence is not board measurement.

| Net | MCU GPIO | LAN8742A pin |
| --- | --- | --- |
| ETH_REF_CLK | PA1 | 14 via R20, 33 ohm |
| ETH_MDIO | PA2 | 12 |
| ETH_MDC | PC1 | 13 |
| ETH_CRS_DV | PA7 | 11 |
| ETH_RXD0 | PC4 | 8 |
| ETH_RXD1 | PC5 | 7 |
| ETH_TX_EN | PG11 | 16 |
| ETH_TXD0 | PG13 | 17 |
| ETH_TXD1 | PG14 | 18 |
| ETH_PHY_RST | PG9 | 15 |

U3 is LAN8742A-CZ-TR; X1 is 25 MHz with C63/C64 15 pF.
R22 pulls nINTSEL low: expected REFCLKO mode. R23 pulls PHYAD0 low:
expected MDIO address 0. Both require reset strap and live MDIO confirmation.
R25 pulls REGOFF low; R24 pulls nRST high; R26 pulls MDIO high.
RXD0/1 and CRS_DV mode straps must not be driven by the MCU during reset.
Check 50 MHz at PA1 after reset before starting MAC/DMA. Do not configure MCO
as a competing source on this network. Confirm populated connector variant.

Reference: [Microchip DS00001989A](https://ww1.microchip.com/downloads/aemDocuments/documents/OTH/ProductDocuments/DataSheets/DS_LAN8742_00001989A.pdf),
sections 3.7 and 4.2.1–4.2.4.

## Portable files and API contract

- Platform/Ethernet/eth_phy.h
- Platform/Ethernet/eth_phy.c

These files depend only on C99 stdint/stddef. MDIO read/write callbacks must
return zero on success, have bounded hardware timeouts, and be serialized by
the caller. PHY calls are task-context operations. Reset polling is nonblocking;
the caller must enforce an elapsed-time deadline and stop on MDIO errors.
Hardware reset, clocks, pin alternate functions and negotiated speed/duplex
configuration are not implemented in this checkpoint. Link status alone must
never be used to select MAC speed/duplex. No automatic address scan is performed.

## Ownership and cache integration requirements (not implemented)

No TX/RX descriptor or buffer allocation exists yet. Before integrating DMA,
reserve a dedicated, DMA-accessible SRAM area in a separate configuration commit;
verify both CM7 and CM4 maps. Current default CM7 RW/ZI is DTCM at 0x20000000;
never place Ethernet DMA descriptors or payloads there. Existing shared IPC
at 0x38000000 and SDMMC DMA allocation must retain their ownership.

Planned first integration uses dedicated noncacheable descriptor and buffer
pools with an explicit MPU region and linker section. Validate region priority,
alignment, size, memory type and cache attributes before enabling DMA.
No address or MPU region number is reserved by this checkpoint.

Planned copy-mode TX: caller owns input until synchronous copy; driver owns
copy until DMA completion/reclaim. Submission failure returns caller ownership;
never reuse a DMA-owned slot. Planned RX: DMA owns posted buffer; CPU may access
only after completion; copy into an lwIP-owned pbuf before reposting the buffer.
Do not introduce zero-copy until reference lifetimes, reset/error reclamation
and backpressure have been tested. Ownership transitions need appropriate
barriers even with noncacheable memory. A future cacheable pool requires cache
line isolation, clean before TX DMA, and carefully ordered RX maintenance;
this report does not claim that cache consistency has been verified.

## Verification checkpoint

- Cortex-M7 compile: ARM Compiler 6 armclang, C99, -Wall -Wextra -Werror: PASS.
- Host execution: MSVC 14.41 x64, /W4 /WX /std:c11: PASS.
- Tests cover ID/revision acceptance, absent PHY IDs, invalid arguments,
  reset busy/completion, latch-low double read and MDIO failure propagation.
- Reproduce host tests from a Visual Studio x64 Developer Command Prompt:
  `Tests\Ethernet\run_host.cmd`.
- Reproduce cross compile from repository root:
  `D:/32/ARM/ARMCLANG/bin/armclang.exe --target=arm-arm-none-eabi -mcpu=cortex-m7 -mthumb -std=c99 -Wall -Wextra -Werror -IPlatform/Ethernet -c Platform/Ethernet/eth_phy.c -o Tests/Ethernet/build/eth_phy.o`

Host tests use simulated MDIO registers. They do not validate wiring or clocks.
No public project, MPU/cache, RTOS, clock, V2 architecture, Media Graph, ownership
framework, Lifecycle or Supervisor code was changed. No Network Module exists.

## Outstanding delivery and acceptance

MAC/DMA implementation, descriptor handling, bounded recovery, negotiated mode,
RMII BSP, lwIP dependency/port, standalone test firmware, TCP/UDP, board
communication, throughput and long-run tests remain incomplete.
Before flashing, identify the connected target, debug probe and firmware recovery
procedure; obtain the test peer/interface/IP configuration. Record firmware
commit, PHY IDs, negotiated mode, clock measurement, descriptor/error counters,
ping loss/latency, TCP/UDP bytes/second, payload sizes, duration and packet loss.
Run cache-enabled traffic, cable unplug/replug, pool exhaustion and repeated
stop/restart tests. Do not fabricate benchmark or board-validation results.
Test firmware stays under Tests/Ethernet and is not production Application code.
Public configuration modifications must be separate commits with impact notes.
