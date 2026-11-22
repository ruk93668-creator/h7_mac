# ST Support Draft — EP0 IN Preemption

**DRAFT — NOT SUBMITTED.** Prepared for human review. Hardware assumptions are explicitly identified; no production implementation or physical testing is authorized by this document.

**Subject:** STM32H747 USB2_OTG_FS — EP0 IN preemption, endpoint disable and TX FIFO retirement

Dear ST Technical Support Team,

We are developing an experimental Full-Speed RAM MSC firmware with TinyUSB's DWC2 DCD and FreeRTOS on a project targeting STM32H747XIH6. We need authoritative clarification before implementing asynchronous cancellation of an active EP0 IN transfer on receipt of a new SETUP.

## Context and evidence limits

- Peripheral: USB2_OTG_FS, integrated FS PHY, device/slave PIO; USB DMA disabled.
- EP0 maximum packet size: 64 bytes.
- Project target: STM32H747XIH6; the complete physical package marking has not been independently verified.
- Previously user-reported DBGMCU_IDCODE: 0x20036450 (DEV_ID 0x450, REV_ID 0x2003). This is historical evidence, not a new read for this inquiry. We cannot distinguish U from V using this value alone.
- Actual USB GSNPSID: not independently measured.
- References: RM0399 Rev5, October 2026; ES0445 Rev7 available through the official online PDF.
- Software-model verification has been performed. The proposed EP0 cancellation path has not been implemented or physically validated. Unknown stopping conditions retain the old source buffer and fault state.

## H1 — Control IN0 disable rules

RM0399 §60.14.55 p2840 gives DIEPCTLx addresses including x=0, but its introductory wording appears to exclude endpoint0. Pages2907 and2917–2918 describe SETUP-induced NAK and general IN stopping.

For an active control IN0 preempted by a new SETUP, please specify:

1. Whether SNAK / INEPNE / EPDIS / EPDISD apply to IN0, and the exact ordered procedure and exceptions.
2. Whether an already asserted INEPNE with SETUP-induced NAK is valid current-state evidence, and how to distinguish it from stale status without re-enabling old transmission through CNAK.
3. Whether EPDISD is guaranteed after a valid IN0 disable request, and which conditions make it invalid to issue EPDIS.
4. Whether SETUP affects IN0.EPENA automatically, as distinct from the explicitly described control OUT enable clearing.

## H2 — EPENA=0 and resource retirement

These are conditional cases requiring clarification, not claims of newly measured board behavior:

- Old programmed transfer completed and an attributable XFRC was observed.
- Software requested disable and an attributable EPDISD was observed.
- EPENA reads zero without an attributable terminal event.

For each case, what proves the old TX FIFO is no longer being read? What additional conditions permit FIFO0 Flush and software source-buffer retirement? If EPENA is already zero, is any further disable action valid? Please cover single- and multi-packet DATA IN and STATUS IN ZLP separately.

## Event ordering and consecutive SETUP

Please describe guarantees for old XFRC versus requested EPDISD, including both bits observed in one interrupt snapshot. We need to preserve old hardware-event ownership while invalidating the old control request's software eligibility.

For slave PIO, please clarify SETUP_RX(A), SETUP_RX(B), SETUP_DONE and repeated-DONE cases: which FIFO ordering and STUPCNT observations associate the final complete request with its done indication? A software generation cannot replace hardware evidence.

## ES0445 clarification

We can now read Rev7 online, including §2.24.5 p44. Please confirm changes from Rev6, the meaning of its target-mode wording for slave PIO, its applicability to this peripheral and EP0 STATUS IN, and any interaction with cancellation. Please also identify the package-marking method for distinguishing U/V.

We are not using the ZLP transmit workaround as evidence for EPDIS/EPDISD or FIFO retirement.

## Requested outcome

Please provide a register-level procedure or authoritative reference for EPENA=1 and EPENA=0 cases, including prerequisites, completion evidence, event acknowledgement ordering, failure conditions and the point at which the old source is no longer referenced.

Public supporting design notes:

- [Initial EP0 preemption design](https://github.com/ruk93668-creator/h7_mac/blob/edb633d423363b66f79c935e2bdd10e3f6608bbb/Docs/usb-lifecycle/TinyUSB_R2EP0PreemptClosure_DesignAudit.md)
- [H1/H2 evidence and open conditions](https://github.com/ruk93668-creator/h7_mac/blob/58aaeb367923b6a488d421e6e0a15cf26ebf09d3/Docs/usb-lifecycle/TinyUSB_R2EP0PreemptClosure_H1H2_Evidence.md)
- [Official ES0445](https://www.st.com/resource/en/errata_sheet/es0445-stm32h745xig-stm32h755xi-stm32h747xig-stm32h757xi-device-errata-stmicroelectronics.pdf)

We can provide a minimal code excerpt or separately approved read-only register evidence if required. We do not assume that completing software-model tests proves silicon timing.

Thank you for your assistance.
