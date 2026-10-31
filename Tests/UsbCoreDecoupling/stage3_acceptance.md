# USB Stage 3 hardening

## Scope

基于 `949180c` 建立独立分支 `codex/usb-stage3-hardening`。本阶段只处理 USB Device Core / Platform / BSP / Mode Manager 资源边界，不实现 UVC/MSC 协议。

## Implemented changes

1. PIO IN staging
   - 不修改 STM32 HAL/LL。
   - 每个 IN endpoint 使用独立、32-byte 对齐的静态包槽。
   - 每次最多复制一个 endpoint max-packet；尾部补齐到 4-byte word。
   - Bulk/Interrupt 由 DataIn completion 逐包续传；ISO 限制为单包。
   - EP0 每个控制包暂存后仍由 ST USB Core 推进下一控制阶段。
   - 源 buffer 在整个异步传输结束前必须保持有效。

2. Abort / Close
   - 新增 BSP/Platform Abort API。
   - Abort 成功前不释放 Platform busy/缓冲区所有权。
   - Close 先 Abort，再 Flush，再 HAL Close。
   - Abort 成功而 HAL Close 失败时，端点仍 opened，但 busy 清除；硬件已不再引用用户 buffer。
   - OUT RX FIFO 是共享 FIFO；有其他 OUT 接收活动时拒绝单端点 Flush/Abort。

3. Error domains
   - `USB_Device_GetLastResult()` 代表设备级致命错误。
   - 新增 `USB_Device_GetLastOperationResult()` 记录最近一次 LL 操作结果。
   - BUSY、参数错误和 ISO incomplete 不再污染设备级故障。
   - Mode Manager 在 WAIT_CONFIGURED/RUNNING 只响应设备级错误；ERROR 可显式请求受控重连。

4. Descriptor / Endpoint declaration
   - 检查配置总长、描述符边界、接口数量、Alternate Setting 唯一性、端点计数/重复地址、FS 端点地址/属性/包长/间隔。
   - 检查配置和接口字符串索引对应的回调。
   - 保存注册配置中所有 Alternate Setting 的 endpoint requirements。
   - Open 必须精确匹配已注册的地址、传输类型和 max packet。

5. Class private data
   - DeInit 前保存 pUserData。
   - DeInit 失败时恢复原绑定。
   - DeInit 成功后清除旧 Class 私有绑定，防止新 Class 继承旧指针。

6. Mode/Class selection
   - 移除 TEST Class 自身的 SERVICE Initcall。
   - Mode Manager 统一选择并注册默认 TEST。
   - 如果已有上层显式注册 Class，不覆盖它。
   - UVC/MSC 仍返回 NOT_SUPPORTED，等待后续 Class 实现接入。

## Added regression coverage

- Cases 360-362: PIO tail padding / multi-packet continuation / Abort success and failure ownership.
- Cases 363-364: undeclared endpoint Open rejection / Close Abort failure and retry.
- Existing multi-packet case 327 updated to drive all 16 packet completions.
- Existing tests updated for endpoint-scoped transient errors and pUserData clearing.

## Not yet verified

本分支由远程源码编辑生成；当前会话未在目标 Windows/Keil 环境运行 ARMCC5/Unicorn，也未连接 STM32H747 实板。因此：
- 不得把已有 stage2 的 192/192 结果视为 stage3 测试结果。
- 必须在目标开发机运行 `Tests/UsbCoreDecoupling/run_tests.py`。
- 必须进行 CM7 ARMCC5 完整编译，检查 0 Error / 0 Warning。
- 必须实板验证 EP0 枚举、Bulk/Interrupt 多包、Abort/Close、Reset/Disconnect、OUT 并发和 FIFO 行为。
- PCD Abort 的测试桩仅用于软件回归，不替代真实 HAL/PCD 时序验收。

## Important limitation

当前 Mode Manager 只接入 TEST Class。UVC/MSC 的注册入口已集中，但 Class 选择、SET_INTERFACE/Alternate Setting 激活策略、UVC Probe/Commit 和 ISO 视频流均不属于本阶段已完成内容。
