# Media Read-only Identity API — Minimal Draft

基线66d3a0b。当前CameraMjpegFifo_GetFrame创建原始loan并置usb_owned；RetainFrame核对frame描述再置sd_owned返回M；ReleaseFrame根据legacy_index归还原始持有，ReleaseLease按M释放SD。它们均不是只读查询。HasLoan仅全局bool，不提供真实M或分owner信息。

## 最小建议：一个有界快照接口

拟议 `SysResult_t CameraMjpegFifo_ReadLoanIdentities(CameraMjpegLoanIdentitySnapshot_t *out)`，名称与类型待审核，未实现。Media不依赖Application G类型。仅在Camera owner串行调用路径使用，禁止ISR；Camera适配负责task身份校验，不能声称现有Media实现自带全部App owner权限检查。

候选out布局（不是新消息ABI）：current_session:uint32；current_original_valid:uint32；current_original_M:原CameraMjpegLease_t；entry_count:uint32；entries[CAMERA_MJPEG_MAX_LOANS]。每entry为M{session,id}+owner_bits:uint32，其中bit0=原始持帧侧、bit1=SD侧。仅返回任一owner位为真的loan；未用项清零。当前7条，字段原始字节数20+7*12=104B、自然4字节对齐预期；ARMCC5实际sizeof/offset未验证，不能报告为实测。

接口取得一次PRIMASK保存/屏蔽和内存屏障下的只读一致快照，复制原loans/legacy_index/s_loan_session；不触发解析、GetFrame、Retain、Release或状态清理，不改result、owner位、引用数、session或计数器。不调用业务Observer/队列。锁边界与原Media ISR更新模型核对后才可实施；对out参数空值/不合规上下文返回既有错误类，矛盾字段返回协议/状态错误，不构造“无loan成功”。具体错误码最终表待审核。

当未Start/已停止时只能陈述实际当前无owner，不声明硬件Stop成功。current_original_valid必须与s_fifo.loaned、legacy_index及其原始owner位一致，否则错误；正常无当前持有时valid=0。条目M不能从Bridge B或grant.request推算。新session/slot复用时旧M不匹配；旧M不在本次快照中只能说明当前未持有该M，不能单独证明此前Release成功，仍需Camera真实返回事实和关联完整性。

## G关联如何形成

USB Camera在GetFrame成功后、Offer前读快照，取current_original_M，和G/H/B绑定至C；若查不到唯一合法current M，保持资源并报错，不发送未绑定Offer、不擅自Release冒充修复。Media单原始loan约束来自GetFrame的s_fifo.loaned检查，真正绑定仍要核对实际frame描述/当前legacy，不以地址跨会话匹配。

Storage同步Offer在RESERVING时绑定G/S，Retain成功得到M；用快照核对M.sd_owned。Return实际成功后核对SD位不再持有；另一方原始owner仍可继续。G始终来自Camera准入权威，Media只返回事实，不自己解释G或保存第二套资格。

快照支持发现未关联的仍持有条目；若存在owner但找不到其现有G或内部角色关联，核验必须拒绝/NOT_PROVEN，不能把它默认归另一消费者。建立全覆盖关系依赖SchemeA所有入口标签不变量；单独新增此查询不足以证明grant到所有loan一一归属。

## SD-only legacy角色

app_uvc_bridge.c SD-only GetFrame同样先占原始owner再Retain SD，再本地StreamReleaseFrame；该原始位不是USB业务持有。C必须记录角色为INTERNAL_CAMERA，并将临时原始M与触发的SD G/交接关联，不能捏造USB grant。只有真正要经USB Offer的原始持有才关联USB G。角色标签放在原Bridge当前交接的flags/terminal字段，不新增引用账本。

本API可区分原始/SD owner位，不能靠位名直接区分业务USB与内部角色，Application关联负责这一步。SD-only在本地Release失败时仍是未排空内部持有，不可让SD退出核验提前成功。

## 覆盖边界与证明状态

这是JPEG FIFO只读查询，不证明其他Camera格式，也不证明USB payload/DMA已停止读源。非JPEG现有CameraCapture路径需要等价身份查询或明确返回未支持，不允许JPEG空快照放行。设计只读性为DESIGN_DEDUCED，运行与ABI/Map数据为NOT_RUN，整体G关联继续NOT_PROVEN直至新门和真实证据完成。

未来Network只允许单独审核的资源适配，不扩展本轮owner_bits到第三位、不复用原始侧冒充Network；方案A不是修改当前Media引用算法的授权。
