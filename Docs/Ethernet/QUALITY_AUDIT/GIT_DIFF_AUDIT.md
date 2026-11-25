# Q9 Git 完整性

结论 **NEEDS_FIX**。审计对象为本地工作树中的完整候选，已提交基线为 `f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb`。相对该基线至 `67a4271` 共 428 个文件，77,673 行新增、2 行删除，主要为上游 lwIP、测试和证据。快照记录见 snapshot.json。

| 证据 | 内容 |
| --- | --- |
| committed.diff / committed.stat.txt / committed.name-status.txt | 共同基线至 HEAD 完整二进制安全 patch、统计和清单 |
| working.diff / working.stat.txt | HEAD 至当前已跟踪内容 |
| staged.diff / unstaged.diff 与各自统计 | 分别保留暂存区和工作区 |
| untracked.diff / workspace-status.txt | 审计前未跟踪文件从空文件起的完整差异与状态 |
| branch-history.txt / remote.json / main-advance.json | 分支提交、GitHub 查得的远端头以及 main 增量文件 |

HTTP/MJPEG 的旧 Platform 路径当前在已跟踪差异里是删除，Service 新路径在未跟踪差异里是新增；同时审阅两个文件集合。已提交生产树变更集中于 Platform 与 CM7/Core/Src/main.c 的 Ethernet MPU 接入；未发现本分支对 USB、Storage、生产 Camera 或 CM4 OLED 源码的直接改动。V1 BSP 保留在 Tests。PHY 历史保留 `bc4db5a`、`b282c7c`、`4647ac7`、`5d8ed49`。已有测试日志在 Docs/Ethernet/Evidence 中，属于版本化证据。RTE 文件只有工程名注释变化，审核前后 SHA-256 一致，见 snapshot.json。

Git 智能 HTTPS 连接被重置，远端状态通过 GitHub 只读 API 核对。最新 main 有 273 个变更文件，与已提交 Ethernet 差异重叠 `.gitignore`、`MDK-ARM/h7_mac.uvprojx`，与未提交候选重叠正式工程文件。重叠说明必须复核，文本冲突和最新 main 集成结果仍为 NOT_VERIFIED。合并阻塞见 ETH-Q06、ETH-Q10。
