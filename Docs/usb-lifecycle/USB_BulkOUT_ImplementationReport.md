# P1-C — Bulk OUT Implementation Report（C0 gate blocked）

## 实际交付与停止点

VERIFIED：基线03a5cb1793e35ec23728f3a6ebbdc132277ed597，本轮完成C0手册及真实源码审计、寄存器所有权表、时序图、最小接口扩展申请与真实HAL/LL软件证据测试。新增文件仅Docs/usb-lifecycle及Tests/UsbBulkOut。没有修改任何生产源文件或原测试入口，未烧录、未执行实板插拔。

UNRESOLVED / C1暂停：当前HAL公开回调不能证明Global OUT NAK唯一所有权，也不能在同一次GRXSTSP pop/copy期间绑定旧Buffer身份。按任务“无法证明立即停止该实现路径”的门禁，没有新增Begin/Poll/Commit生产API或仅在模型中清busy模拟成功。包括“空闲完成后Stop”的可复用路径也未假定无遗留RX状态而绕过门禁。

VERIFIED源码冲突：HAL PCD :1144–1164允许任意OUT EPDISD解除共享NAK；:1413–1425、:1448–1470是独立ISO NAK使用者；:1080–1109在上层完成回调之前pop并copy。目标DAINT mask不能拦截RXFLVL copy。详见[硬件及源码证据](USB_BulkOUT_HardwareEvidence.md)。

## C1未来分阶段状态（只做设计）

INFERRED：ACTIVE/IDLE→BEGIN_FENCED→CLAIM_GLOBAL_NAK→WAIT_NAK_MARKER→DISABLE_TARGET→DRAIN_RX_AND_EVENTS→RELEASE_OR_RETAIN_NAK→PROVEN→COMMITTED。任何缺失或超时进入RECOVERY_REQUIRED，保留Buffer、身份、busy及原始硬件证据。每次Task Poll只推进有限阶段，不在ISR内长轮询，不因失败关闭整个USB IRQ。

INFERRED：逻辑目标端点参数化，优先测0x02但接口不能包含MSC业务。排除EP0和ISO OUT作为该Bulk接口目标，仍必须保护它们与共享FIFO/Global NAK的交互。没有新增Task或动态内存。

## 原有实现的边界

VERIFIED：旧BSP_USB_Receive（bsp_usb.c:992）只有s_rx_active，无Transfer身份。旧Abort调用HAL/LL后可能Flush共享RX；LL OUT分支（stm32h7xx_ll_usb.c:944–958）等待EPENA而非完整NAK/EPDISD/RX证明。它们保持基线原样，不得称为新C1的安全停止实现。

VERIFIED：新增hal_out_evidence.c把未改HAL PCD与LL编成真实ARM对象，替换的仅为运行库/LPM等外部测试边界。报告区分手册依据、源码事实、软件MMIO分支证据、实板NOT_TESTED。启动期链接/ABI夹具失败保留，不改历史断言。

## 验证和Git

构建、原76项、17入口逐用例对照、C0专项及源码字节保护结果见[软件验证报告](USB_BulkOUT_SoftwareVerification.md)。历史FAIL保留，不以退出码相同代替用例对比。产物及中间快照保留在output/MSC24/udlm-p1c，不提交二进制/PDF。

UNRESOLVED：初始远端核对多次遇到GitHub连接重置/443超时，本地HEAD及干净工作区已确认。Git交付只允许普通push，不强推，不fetch后重置；若远端变化或网络仍失败，保留本地提交并如实报告，禁止宣称远端一致。

完成本轮C0及可执行软件验证后停止，等待[最小扩展申请](USB_BulkOUT_MinimalInterfaceExtension_Approval.md)审核，不进入MSC BOT迁移。
