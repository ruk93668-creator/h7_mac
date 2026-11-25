# Ethernet 合并前审核

**结论：NOT_READY_FOR_MERGE**

任务开始时，工作分支为 `codex/ethernet-bringup`。本地 HEAD 和 GitHub 分支均为 `a1fbe31a407042c1cea1b9c7d0abde733afd741f`，GitHub main 为 `f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb`。工作区原有 `MDK-ARM/RTE/_h7_mac_CM7/RTE_Components.h` 项目名称注释修改，原 SHA-256 为 `cc73360e44310f9a23b21318abdb107b979d3425a8ba879de9fd2ba8dfbec92f`。

## 变更与默认状态

- 移动 `eth_http.c/h` 和 `eth_mjpeg.c/h` 至 `CM7/Service/Ethernet/`。对外接口和源码实现沿用原版本。
- 新增 Ethernet 平台时基与正式断言适配，正式 CM7 工程和独立验证对接公共时基。
- 接入正式 CM7 链接依赖，预留 lwIP CPU 静态内存。正式原业务启动由原入口执行。Ethernet 运行、HTTP/MJPEG 服务、E7 诊断配置和跨任务消费默认处于禁用状态。
- 新增本轮五份 Markdown 审核。历史源码、验证证据和测试数据均保留。除四个文件的路径迁移外，本轮没有删除源文件。

详细生产清单及内存见 [BUILD_INTEGRATION.md](BUILD_INTEGRATION.md)，分层和运行契约见 [PRODUCTION_ARCHITECTURE.md](PRODUCTION_ARCHITECTURE.md)，测试和原始证据见 [TEST_INVENTORY.md](TEST_INVENTORY.md)，未关闭门禁见 [REMAINING_RISKS.md](REMAINING_RISKS.md)。

## 验证及审核材料

正式 CM7 原基线、正式 CM7 当前配置、正式 CM4、独立 E5–E7 构建，以及 E4.1–E7 Host 回归均 PASS。链接边界检查通过，新增正式模块当前为链接待命。正式硬件回归 NOT_RUN，跨任务租约、网络停止与重启契约 BLOCKED。E7 性能未达到 20 fps，DMA FIFO 根因待关闭。

main 是任务起点的祖先，开始时分支领先 main 53 个提交。审核补丁位于 `output/Ethernet/merge_readiness/candidate_vs_head.diff`（本轮）及 `output/Ethernet/merge_readiness/candidate_vs_main.diff`（完整候选）。两份补丁均排除了用户 RTE 修改。本地候选待人工审核。
