# Phase 2 — Media Resource Implementation Plan（规划基线；本轮独立软件实施已授权，真实适配待审）

输入bd24c03，沿用501330a全部纯基础实现和Phase1有条件验收。依据用户2026-11-17最终冻结路线同步；不是新架构或阶段重排。阶段号已确定，不再列为待决。接口/布局/容量仍待审核。

## 1. 实施依赖与工作包

| 顺序 | 拟修改位置/责任 | 验收及依赖 |
|---|---|---|
| MR-A | Resource/Ports：Buffer Source只读能力/身份查询；既有Raw CameraStream和JPEG FIFO差异契约 | 先审核底层最小ADAPT范围；无Loan副作用。Raw7slot单IN_USE与JPEG双span/块引用不抹平，不重复建池 |
| MR-B | Resource关联：Consumer identity、Grant G、独立RELEASE R、交接H、真实M和Storage S | A后绑定，Grant来源沿用权威资格，不复制资格状态机/物理引用计数；错owner/旧代次/耗尽拒绝 |
| MR-C | Resource准入：同owner L_admit/L_close/在途记录 | 依赖B；关后同G不再新Get/Retain，已受理工作排空，迟到通知不重开 |
| MR-D | Resource交接/Terminal Receipt/ACK消费与退休 | 可与C开发，但依赖B身份；原ACK队列/Tx/Rx/Replay规则保持，队列满重试原身份；T不持Frame、不覆盖SD-only历史证据 |
| MR-E | Resource退出核验/DMA端口 | 依赖A～D；逐消费者真实Backend归还、所有原始CPU/DMA读取结束、终结消费、内部持有逐项证明 |
| MR-F | 联合软件/实板门禁与预算 | O0/O2、负对照、两核/Map/ABI/栈，按授权硬件固件验证；未执行即NOT_RUN |

每工作包独立提交。没有生产修改授权前不得开始；先固定真实后端能力，Raw多owner复制/后端扩展及第三consumer能力均待专项决策。新模块声明可扩展但不重写Resolver/Graph/Format。Phase1别名/单owner/栈保留项在实际调用上下文设计时核对，不以Resource实现为借口修改Phase1或旧协议。

## 2. 安全门禁

Raw Pool复用camera_capture.c中的固定物理slot及DMA M0/M1状态；JPEG复用FIFO块和实际usb_owned/sd_owned。统一的是视图和身份/请求契约，不是物理分配策略。Buffer Source不能替CameraCapture重新控制DCMI；Media Graph只拓扑，Resource管实际交接/归还，Lifecycle不替代所有权，Supervisor不直接Device/BSP/HAL。

完整G→H→M关联与稳定关闭两个NOT_PROVEN必须通过真实源码、可复现轨迹、负对照和必要硬件证据逐个关闭。RETURN_PENDING、文件关闭、ACK入队、发送返回、客户端断线都不能代替归还。DMA完成/Abort成功和每个并行原始读取者分别核验；错误或超时保留资源事实，不强清。永久存储文件错误与资格/帧退出结果分开。

## 3. 后续边界、预算及回退

Phase3执行协调、Phase4/5/6 Camera/USB/Storage模块迁移、Phase7统一控制、Phase8真实重配置、Phase9扩展、Phase10最终清理均未在本轮实施。允许设计适配端口，不偷偷接旧启动。原资源预算的576B仅候选元数据估算，须ARMCC5 sizeof/Map/栈证明；不擅扩任务/队列，不复制Raw/JPEG SDRAM池。

纯未活动V2实现可逐包回退；真实硬件绑定须独立授权并以整固件回滚，不能新旧owner混用。遗留实现不在Phase2删除。32项新增矩阵与原72项仅按集合身份保留，覆盖交叉表明确重叠，不累计为104项独立资源验收。

## 4. 本轮授权与实际落地

本轮用户已授权MR-A～MR-F独立软件基础，源码/测试/构建见Phase2_ImplementationReport.md。真实Legacy端口及双端运输/ACK消费发布未绑定，最小待审差异见Phase2_BackendAdaptationGate.md；该部分不因本轮软件结果获得修改许可。原32项和72项NOT_RUN及两个NOT_PROVEN保持，不以Mock关闭。
