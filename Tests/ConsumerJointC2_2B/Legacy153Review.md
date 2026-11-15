# B2/B3 153项逐项复核

本轮统计：{'total': 153, 'previous_PASS': 24, 'status_counts': {'PASS': 24, 'BLOCKED': 128, 'FAIL': 1}, 'classification_counts': {'D': 128, 'C': 1}, 'no_automatic_promotion': True, 'C_assertion_count': 153}。历史完整轨迹门禁24 PASS/129未通过记录原样保留。

C断言通过不等于完整验收；正常新增异步事件不作为first_contract_violation。BLOCKED表示该ID的原Python诊断/结果/交错谓词尚未获得等价契约证据，不是已证明的生产缺陷。

| Suite/ID | 场景 | C断言 | 完整验收 | 分类 | 原始轨迹 |
|---|---|---|---|---|---|
| b2/0 | BOOT gate | PASS | PASS | — | [0](Evidence/legacy_pairs/b2_000.json.gz) |
| b2/1 | Owner gate | PASS | PASS | — | [1](Evidence/legacy_pairs/b2_001.json.gz) |
| b2/2 | STOP before START | PASS | PASS | — | [2](Evidence/legacy_pairs/b2_002.json.gz) |
| b2/3 | Unknown command | PASS | PASS | — | [3](Evidence/legacy_pairs/b2_003.json.gz) |
| b2/4 | START IO failure | PASS | BLOCKED | D | [4](Evidence/legacy_pairs/b2_004.json.gz) |
| b2/5 | Not initialized | PASS | BLOCKED | D | [5](Evidence/legacy_pairs/b2_005.json.gz) |
| b2/6 | ACK failure with FAULT | PASS | BLOCKED | D | [6](Evidence/legacy_pairs/b2_006.json.gz) |
| b2/7 | Configured after START | PASS | BLOCKED | D | [7](Evidence/legacy_pairs/b2_007.json.gz) |
| b2/8 | Repeated START BUSY fault | PASS | BLOCKED | D | [8](Evidence/legacy_pairs/b2_008.json.gz) |
| b2/9 | Mode error same-round cancel | PASS | BLOCKED | D | [9](Evidence/legacy_pairs/b2_009.json.gz) |
| b2/10 | Abort failure ownership | PASS | FAIL | C | [10](Evidence/legacy_pairs/b2_010.json.gz) |
| b2/11 | Bridge Return ACK wait | PASS | BLOCKED | D | [11](Evidence/legacy_pairs/b2_011.json.gz) |
| b2/12 | Mode Stop IO retry | PASS | BLOCKED | D | [12](Evidence/legacy_pairs/b2_012.json.gz) |
| b2/13 | Mode Stop BUSY retry | PASS | BLOCKED | D | [13](Evidence/legacy_pairs/b2_013.json.gz) |
| b2/14 | Stream query failure | PASS | BLOCKED | D | [14](Evidence/legacy_pairs/b2_014.json.gz) |
| b2/15 | Class absent on STOP | PASS | BLOCKED | D | [15](Evidence/legacy_pairs/b2_015.json.gz) |
| b2/16 | EOF FPS sample | PASS | BLOCKED | D | [16](Evidence/legacy_pairs/b2_016.json.gz) |
| b2/17 | Tick and EOF wrap | PASS | BLOCKED | D | [17](Evidence/legacy_pairs/b2_017.json.gz) |
| b2/18 | FPS overflow guard | PASS | BLOCKED | D | [18](Evidence/legacy_pairs/b2_018.json.gz) |
| b2/19 | Task wait error retained | PASS | BLOCKED | D | [19](Evidence/legacy_pairs/b2_019.json.gz) |
| b2/20 | SOF coalescing | PASS | BLOCKED | D | [20](Evidence/legacy_pairs/b2_020.json.gz) |
| b2/21 | SOF wrap | PASS | BLOCKED | D | [21](Evidence/legacy_pairs/b2_021.json.gz) |
| b2/22 | ACK and FAULT independent retry | PASS | BLOCKED | D | [22](Evidence/legacy_pairs/b2_022.json.gz) |
| b2/23 | STOP START FIFO | PASS | BLOCKED | D | [23](Evidence/legacy_pairs/b2_023.json.gz) |
| b2/24 | Repeated STOP | PASS | BLOCKED | D | [24](Evidence/legacy_pairs/b2_024.json.gz) |
| b2/25 | EOF event observation (stub) | PASS | BLOCKED | D | [25](Evidence/legacy_pairs/b2_025.json.gz) |
| b2/26 | Frame loan then STOP | PASS | BLOCKED | D | [26](Evidence/legacy_pairs/b2_026.json.gz) |
| b2/27 | Wait and stack cadence | PASS | BLOCKED | D | [27](Evidence/legacy_pairs/b2_027.json.gz) |
| b2/28 | Task stack and priority | PASS | BLOCKED | D | [28](Evidence/legacy_pairs/b2_028.json.gz) |
| b2/29 | Duplicate task create | PASS | BLOCKED | D | [29](Evidence/legacy_pairs/b2_029.json.gz) |
| b2/30 | Task allocation failure | PASS | BLOCKED | D | [30](Evidence/legacy_pairs/b2_030.json.gz) |
| b2/31 | Fault report reset on START | PASS | BLOCKED | D | [31](Evidence/legacy_pairs/b2_031.json.gz) |
| b2/32 | BOOT gate | PASS | PASS | — | [32](Evidence/legacy_pairs/b2_032.json.gz) |
| b2/33 | Owner gate | PASS | PASS | — | [33](Evidence/legacy_pairs/b2_033.json.gz) |
| b2/34 | STOP before START | PASS | PASS | — | [34](Evidence/legacy_pairs/b2_034.json.gz) |
| b2/35 | Unknown command | PASS | PASS | — | [35](Evidence/legacy_pairs/b2_035.json.gz) |
| b2/36 | START IO failure | PASS | BLOCKED | D | [36](Evidence/legacy_pairs/b2_036.json.gz) |
| b2/37 | Not initialized | PASS | BLOCKED | D | [37](Evidence/legacy_pairs/b2_037.json.gz) |
| b2/38 | ACK failure with FAULT | PASS | BLOCKED | D | [38](Evidence/legacy_pairs/b2_038.json.gz) |
| b2/39 | Configured after START | PASS | BLOCKED | D | [39](Evidence/legacy_pairs/b2_039.json.gz) |
| b2/40 | Repeated START BUSY fault | PASS | BLOCKED | D | [40](Evidence/legacy_pairs/b2_040.json.gz) |
| b2/41 | Mode error same-round cancel | PASS | BLOCKED | D | [41](Evidence/legacy_pairs/b2_041.json.gz) |
| b2/42 | Abort failure ownership | PASS | BLOCKED | D | [42](Evidence/legacy_pairs/b2_042.json.gz) |
| b2/43 | Bridge Return ACK wait | PASS | BLOCKED | D | [43](Evidence/legacy_pairs/b2_043.json.gz) |
| b2/44 | Mode Stop IO retry | PASS | BLOCKED | D | [44](Evidence/legacy_pairs/b2_044.json.gz) |
| b2/45 | Mode Stop BUSY retry | PASS | BLOCKED | D | [45](Evidence/legacy_pairs/b2_045.json.gz) |
| b2/46 | Stream query failure | PASS | BLOCKED | D | [46](Evidence/legacy_pairs/b2_046.json.gz) |
| b2/47 | Class absent on STOP | PASS | BLOCKED | D | [47](Evidence/legacy_pairs/b2_047.json.gz) |
| b2/48 | EOF FPS sample | PASS | BLOCKED | D | [48](Evidence/legacy_pairs/b2_048.json.gz) |
| b2/49 | Tick and EOF wrap | PASS | BLOCKED | D | [49](Evidence/legacy_pairs/b2_049.json.gz) |
| b2/50 | FPS overflow guard | PASS | BLOCKED | D | [50](Evidence/legacy_pairs/b2_050.json.gz) |
| b2/51 | Task wait error retained | PASS | BLOCKED | D | [51](Evidence/legacy_pairs/b2_051.json.gz) |
| b2/52 | SOF coalescing | PASS | BLOCKED | D | [52](Evidence/legacy_pairs/b2_052.json.gz) |
| b2/53 | SOF wrap | PASS | BLOCKED | D | [53](Evidence/legacy_pairs/b2_053.json.gz) |
| b2/54 | ACK and FAULT independent retry | PASS | BLOCKED | D | [54](Evidence/legacy_pairs/b2_054.json.gz) |
| b2/55 | STOP START FIFO | PASS | BLOCKED | D | [55](Evidence/legacy_pairs/b2_055.json.gz) |
| b2/56 | Repeated STOP | PASS | BLOCKED | D | [56](Evidence/legacy_pairs/b2_056.json.gz) |
| b2/57 | EOF event observation (stub) | PASS | BLOCKED | D | [57](Evidence/legacy_pairs/b2_057.json.gz) |
| b2/58 | Frame loan then STOP | PASS | BLOCKED | D | [58](Evidence/legacy_pairs/b2_058.json.gz) |
| b2/59 | Wait and stack cadence | PASS | BLOCKED | D | [59](Evidence/legacy_pairs/b2_059.json.gz) |
| b2/60 | Task stack and priority | PASS | BLOCKED | D | [60](Evidence/legacy_pairs/b2_060.json.gz) |
| b2/61 | Duplicate task create | PASS | BLOCKED | D | [61](Evidence/legacy_pairs/b2_061.json.gz) |
| b2/62 | Task allocation failure | PASS | BLOCKED | D | [62](Evidence/legacy_pairs/b2_062.json.gz) |
| b2/63 | Fault report reset on START | PASS | BLOCKED | D | [63](Evidence/legacy_pairs/b2_063.json.gz) |
| b2/64 | BOOT gate | PASS | PASS | — | [64](Evidence/legacy_pairs/b2_064.json.gz) |
| b2/65 | Owner gate | PASS | PASS | — | [65](Evidence/legacy_pairs/b2_065.json.gz) |
| b2/66 | STOP before START | PASS | PASS | — | [66](Evidence/legacy_pairs/b2_066.json.gz) |
| b2/67 | Unknown command | PASS | PASS | — | [67](Evidence/legacy_pairs/b2_067.json.gz) |
| b2/68 | START IO failure | PASS | BLOCKED | D | [68](Evidence/legacy_pairs/b2_068.json.gz) |
| b2/69 | Not initialized | PASS | BLOCKED | D | [69](Evidence/legacy_pairs/b2_069.json.gz) |
| b2/70 | ACK failure with FAULT | PASS | BLOCKED | D | [70](Evidence/legacy_pairs/b2_070.json.gz) |
| b2/71 | Configured after START | PASS | BLOCKED | D | [71](Evidence/legacy_pairs/b2_071.json.gz) |
| b2/72 | Repeated START BUSY fault | PASS | BLOCKED | D | [72](Evidence/legacy_pairs/b2_072.json.gz) |
| b2/73 | Mode error same-round cancel | PASS | BLOCKED | D | [73](Evidence/legacy_pairs/b2_073.json.gz) |
| b2/74 | Abort failure ownership | PASS | BLOCKED | D | [74](Evidence/legacy_pairs/b2_074.json.gz) |
| b2/75 | Bridge Return ACK wait | PASS | BLOCKED | D | [75](Evidence/legacy_pairs/b2_075.json.gz) |
| b2/76 | Mode Stop IO retry | PASS | BLOCKED | D | [76](Evidence/legacy_pairs/b2_076.json.gz) |
| b2/77 | Mode Stop BUSY retry | PASS | BLOCKED | D | [77](Evidence/legacy_pairs/b2_077.json.gz) |
| b2/78 | Stream query failure | PASS | BLOCKED | D | [78](Evidence/legacy_pairs/b2_078.json.gz) |
| b2/79 | Class absent on STOP | PASS | BLOCKED | D | [79](Evidence/legacy_pairs/b2_079.json.gz) |
| b2/80 | EOF FPS sample | PASS | BLOCKED | D | [80](Evidence/legacy_pairs/b2_080.json.gz) |
| b2/81 | Tick and EOF wrap | PASS | BLOCKED | D | [81](Evidence/legacy_pairs/b2_081.json.gz) |
| b2/82 | FPS overflow guard | PASS | BLOCKED | D | [82](Evidence/legacy_pairs/b2_082.json.gz) |
| b2/83 | Task wait error retained | PASS | BLOCKED | D | [83](Evidence/legacy_pairs/b2_083.json.gz) |
| b2/84 | SOF coalescing | PASS | BLOCKED | D | [84](Evidence/legacy_pairs/b2_084.json.gz) |
| b2/85 | SOF wrap | PASS | BLOCKED | D | [85](Evidence/legacy_pairs/b2_085.json.gz) |
| b2/86 | ACK and FAULT independent retry | PASS | BLOCKED | D | [86](Evidence/legacy_pairs/b2_086.json.gz) |
| b2/87 | STOP START FIFO | PASS | BLOCKED | D | [87](Evidence/legacy_pairs/b2_087.json.gz) |
| b2/88 | Repeated STOP | PASS | BLOCKED | D | [88](Evidence/legacy_pairs/b2_088.json.gz) |
| b2/89 | EOF event observation (stub) | PASS | BLOCKED | D | [89](Evidence/legacy_pairs/b2_089.json.gz) |
| b2/90 | Frame loan then STOP | PASS | BLOCKED | D | [90](Evidence/legacy_pairs/b2_090.json.gz) |
| b2/91 | Wait and stack cadence | PASS | BLOCKED | D | [91](Evidence/legacy_pairs/b2_091.json.gz) |
| b2/92 | Task stack and priority | PASS | BLOCKED | D | [92](Evidence/legacy_pairs/b2_092.json.gz) |
| b2/93 | Duplicate task create | PASS | BLOCKED | D | [93](Evidence/legacy_pairs/b2_093.json.gz) |
| b2/94 | Task allocation failure | PASS | BLOCKED | D | [94](Evidence/legacy_pairs/b2_094.json.gz) |
| b2/95 | Fault report reset on START | PASS | BLOCKED | D | [95](Evidence/legacy_pairs/b2_095.json.gz) |
| b2/96 | NULL command | PASS | BLOCKED | D | [96](Evidence/legacy_pairs/b2_096.json.gz) |
| b2/97 | NULL reply | PASS | BLOCKED | D | [97](Evidence/legacy_pairs/b2_097.json.gz) |
| b2/98 | Pure getters and ACK confirmation | PASS | BLOCKED | D | [98](Evidence/legacy_pairs/b2_098.json.gz) |
| b2/99 | FAULT confirmation preserves ACK | PASS | BLOCKED | D | [99](Evidence/legacy_pairs/b2_099.json.gz) |
| b2/100 | ACK confirmation preserves FAULT | PASS | BLOCKED | D | [100](Evidence/legacy_pairs/b2_100.json.gz) |
| b2/101 | Diagnostic observer is optional | PASS | BLOCKED | D | [101](Evidence/legacy_pairs/b2_101.json.gz) |
| b3/0 | NULL prepare | PASS | PASS | — | [0](Evidence/legacy_pairs/b3_000.json.gz) |
| b3/1 | Invalid config | PASS | PASS | — | [1](Evidence/legacy_pairs/b3_001.json.gz) |
| b3/2 | Unprepared request | PASS | PASS | — | [2](Evidence/legacy_pairs/b3_002.json.gz) |
| b3/3 | Duplicate prepare | PASS | PASS | — | [3](Evidence/legacy_pairs/b3_003.json.gz) |
| b3/4 | Command queue full | PASS | BLOCKED | D | [4](Evidence/legacy_pairs/b3_004.json.gz) |
| b3/5 | Non JPEG rejection | PASS | PASS | — | [5](Evidence/legacy_pairs/b3_005.json.gz) |
| b3/6 | TEST rejection | PASS | PASS | — | [6](Evidence/legacy_pairs/b3_006.json.gz) |
| b3/7 | Context and NULL status | PASS | PASS | — | [7](Evidence/legacy_pairs/b3_007.json.gz) |
| b3/8 | Mount failure recovery | PASS | BLOCKED | D | [8](Evidence/legacy_pairs/b3_008.json.gz) |
| b3/9 | Space query failure recovery | PASS | BLOCKED | D | [9](Evidence/legacy_pairs/b3_009.json.gz) |
| b3/10 | Recover outside FAULT | PASS | PASS | — | [10](Evidence/legacy_pairs/b3_010.json.gz) |
| b3/11 | STOP before start | PASS | PASS | — | [11](Evidence/legacy_pairs/b3_011.json.gz) |
| b3/12 | Four request FIFO | PASS | BLOCKED | D | [12](Evidence/legacy_pairs/b3_012.json.gz) |
| b3/13 | Snapshot complete | PASS | BLOCKED | D | [13](Evidence/legacy_pairs/b3_013.json.gz) |
| b3/14 | Two-span snapshot | PASS | BLOCKED | D | [14](Evidence/legacy_pairs/b3_014.json.gz) |
| b3/15 | Readback corruption | PASS | BLOCKED | D | [15](Evidence/legacy_pairs/b3_015.json.gz) |
| b3/16 | Read failure | PASS | BLOCKED | D | [16](Evidence/legacy_pairs/b3_016.json.gz) |
| b3/17 | Snapshot close failure | PASS | BLOCKED | D | [17](Evidence/legacy_pairs/b3_017.json.gz) |
| b3/18 | Two-span JPEG CRC | PASS | BLOCKED | D | [18](Evidence/legacy_pairs/b3_018.json.gz) |
| b3/19 | No-frame timeout | PASS | BLOCKED | D | [19](Evidence/legacy_pairs/b3_019.json.gz) |
| b3/20 | Repeated START | PASS | BLOCKED | D | [20](Evidence/legacy_pairs/b3_020.json.gz) |
| b3/21 | STOP admission gate | PASS | BLOCKED | D | [21](Evidence/legacy_pairs/b3_021.json.gz) |
| b3/22 | Generation exhausted | PASS | BLOCKED | D | [22](Evidence/legacy_pairs/b3_022.json.gz) |
| b3/23 | Retain failure | PASS | BLOCKED | D | [23](Evidence/legacy_pairs/b3_023.json.gz) |
| b3/24 | Occupied slot | PASS | BLOCKED | D | [24](Evidence/legacy_pairs/b3_024.json.gz) |
| b3/25 | JPEG invalid | PASS | BLOCKED | D | [25](Evidence/legacy_pairs/b3_025.json.gz) |
| b3/26 | Open failure | PASS | BLOCKED | D | [26](Evidence/legacy_pairs/b3_026.json.gz) |
| b3/27 | AVI partial failure | PASS | BLOCKED | D | [27](Evidence/legacy_pairs/b3_027.json.gz) |
| b3/28 | Short write | PASS | BLOCKED | D | [28](Evidence/legacy_pairs/b3_028.json.gz) |
| b3/29 | Periodic sync failure | PASS | BLOCKED | D | [29](Evidence/legacy_pairs/b3_029.json.gz) |
| b3/30 | Old session | PASS | BLOCKED | D | [30](Evidence/legacy_pairs/b3_030.json.gz) |
| b3/31 | Old generation | PASS | BLOCKED | D | [31](Evidence/legacy_pairs/b3_031.json.gz) |
| b3/32 | Queued frame then STOP | PASS | BLOCKED | D | [32](Evidence/legacy_pairs/b3_032.json.gz) |
| b3/33 | Release failure retry | PASS | BLOCKED | D | [33](Evidence/legacy_pairs/b3_033.json.gz) |
| b3/34 | Wrong owner return | PASS | BLOCKED | D | [34](Evidence/legacy_pairs/b3_034.json.gz) |
| b3/35 | Repeated return | PASS | BLOCKED | D | [35](Evidence/legacy_pairs/b3_035.json.gz) |
| b3/36 | Cached status timing | PASS | BLOCKED | D | [36](Evidence/legacy_pairs/b3_036.json.gz) |
| b3/37 | Tick wrap | PASS | BLOCKED | D | [37](Evidence/legacy_pairs/b3_037.json.gz) |
| b3/38 | AVI Finish write failure | PASS | BLOCKED | D | [38](Evidence/legacy_pairs/b3_038.json.gz) |
| b3/39 | AVI Close failure | PASS | BLOCKED | D | [39](Evidence/legacy_pairs/b3_039.json.gz) |
| b3/40 | Unmount failure | PASS | BLOCKED | D | [40](Evidence/legacy_pairs/b3_040.json.gz) |
| b3/41 | AVI Header seek failure | PASS | BLOCKED | D | [41](Evidence/legacy_pairs/b3_041.json.gz) |
| b3/42 | Repeated STOP | PASS | BLOCKED | D | [42](Evidence/legacy_pairs/b3_042.json.gz) |
| b3/43 | Static task create | PASS | PASS | — | [43](Evidence/legacy_pairs/b3_043.json.gz) |
| b3/44 | Allocation failure | PASS | PASS | — | [44](Evidence/legacy_pairs/b3_044.json.gz) |
| b3/45 | BOOT wait and stack cadence | PASS | BLOCKED | D | [45](Evidence/legacy_pairs/b3_045.json.gz) |
| b3/46 | Prepare failure gate | PASS | PASS | — | [46](Evidence/legacy_pairs/b3_046.json.gz) |
| b3/47 | Ready authority and pure getters | PASS | BLOCKED | D | [47](Evidence/legacy_pairs/b3_047.json.gz) |
| b3/48 | FPS private counter | PASS | BLOCKED | D | [48](Evidence/legacy_pairs/b3_048.json.gz) |
| b3/49 | Snapshot private filename | PASS | BLOCKED | D | [49](Evidence/legacy_pairs/b3_049.json.gz) |
| b3/50 | Task gate ignores diagnostic ready tamper | PASS | BLOCKED | D | [50](Evidence/legacy_pairs/b3_050.json.gz) |
