# P1-C1 — 尚未证明条件与迁移门禁

VERIFIED：默认 HAL 未绑定路径维持原行为；C1 是显式 opt-in 的 endpoint 机制，不是 MSC 整机重连修复交付。生产配置未自动调用 Enable；不能从 MAP 中有 Hook 符号推断其运行时已启用。

| 条件 | 状态 | 本轮处理与后续门禁 |
| --- | --- | --- |
| 真实 RX copy / W1C 前事实 / 全部当前 NAK 写者 | VERIFIED（源码及模型） | 完整 Hook 表接管，独立 HAL 专项先于停止测试；旧路径24项单独保留 |
| FIFO marker 与 Disable 的硅片时序 | INFERRED；实板 NOT_TESTED | 代码遵从 RM 条件；模型只按真实寄存器命令推进，不能证明硅片时延和极端竞争 |
| OTG core GSNPSID | UNRESOLVED（实物） | 测试寄存器值只是模型输入；旧 DBGMCU REV2003/U-V 不替代 core ID；不得区分 U 与 V |
| Reset/Disconnect 时仍有旧 owner | VERIFIED（保留） | Session 失效但不释放；进入 RECOVERY_REQUIRED；没有自动复活/PCD 重启或 MSC 重连声明 |
| 普通 EP0 交叠取消、完整控制生命周期 | UNRESOLVED（通用成功恢复） | C1 不对 EP0 EPDIS，不覆写旧 owner；新的 SETUP 数据用永久 Setup 区，旧控制数据 owner 冲突则故障保留并否决旧业务完成。不是已完成的 EP0 状态机迁移 |
| 内部 SETUP 接收的 ZLP 与任意交叠 RX | UNRESOLVED（全部恢复分支） | 常规 ZLP 两条路径有测试；无法给出匹配身份时保守不归还、不猜测完成。不得宣称所有 EP0/ZLP 路径已验证 |
| ISO OUT 停止 | VERIFIED（当前 NAK 参与和 disable 模型） | 原 HAL ISO writer 成套接管；不是通用 ISO OUT/UVC 流验收。真实 ISO 抖动/期限 NOT_TESTED |
| ISO IN / UVC Frame Lease | NOT_TESTED（迁移） | 原路径未改；本轮不处理 UVC 类生命周期或已有历史 FAIL |
| ES0445 Rev7 §2.24.5 Slave/PIO ZLP | UNRESOLVED（消除） | 未越权修改 LL/协议；沿用 P1-B.1 勘误适用性限制，不声称覆盖所有 ZLP |
| 外部 SDRAM / USB DMA / SD IDMA | NOT_TESTED；接口不支持前两者 | 本轮只支持 CPU 内部可写 SRAM 的 PIO RX；USB proof 独立于 Storage/DMA proof |
| 恶意外部直接写私有 PCD/寄存器 | UNRESOLVED（不受支持） | 当前生产 PCD 私有，正常 HAL/BSP 写者已审计；外部调试写或越层调用不在唯一 owner 契约内。观察到屏障丢失即锁定 |
| MSC BOT / Service 集成 | NOT_TESTED（未迁移） | 后续审批并协调 Task Prepare、BOT Reset、Class DeInit、Worker 引用后才能自动启用；禁止为了演示重连绕过本门禁 |

VERIFIED：本轮软件用例通过只证明指定模型、实际 ARM HAL/BSP/Platform 程序及故障分支。未闭合的硬件与通用 EP0 恢复条件保留为验收限制，不能宣称完整 USB 生命周期或硬件验收通过。旧测试 FAIL 和首次夹具失败报告都保留，不以全绿替代审查。
