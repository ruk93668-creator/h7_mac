# 唯一 FAIL：B2 ID10 / UVC Abort Failure

原门禁唯一 FAIL 完整冻结于 RawResults/Frozen/unique_fail.json。原始 C 返回值通过不代表 Consumer 清退正确。

首次 STOP Cancel 返回 IO → 原 STOP ACK IO 入队 → stop_command 清除 → 后台 Cancel/Return/ACK 最终成功 → USB ModeStop 成功。旧 3cf288f 把 ConsumerEnd 绑定 stop_command，因此最终 phase=GRANTED(3)、grant=1，未提交 RELEASE。

以旧生产 app_usb.c 加同一当前生命周期夹具复现，O0/O2 均 C return=0，但新因果检查明确 FAIL；当前 177c0e1 均 phase=RELEASE(4)、grant=1，检查 PASS。见 UniqueFail.json、old_id7_results.json、RawResults/OldID7Reproduction/ 和对应 Current 原始记录。首个契约违例是后台停止完成后缺少 RELEASE，而非首次 ACK IO。

生产修复归属 8a83591，当前基线已包含。本轮未改 app_usb.c。ConsumerEnd 与 stop_command 分离；仅在 Bridge Idle、ModeStop 完成、USB 不再运行或停止且仍持 grant/GRANTED 时推进。已 Stage 不重复分配请求号，未 Stage 可重试。原始错误和 ACK/FAULT 独立交付保留。

根因关闭，ID7 22/22、联合 ID7 22/22、B2 C 入口 O0/O2 各 102/102 复验。完整旧 Python 仍为 16/102，原 b2/10 历史 FAIL 未重写；新架构映射标为 CONTRACT_CHANGED。grant 仍为 1 是安全阻塞，不是 RELEASE 成功；未生成伪造 QUALIFICATION_RELEASED，不代表 C2.2C 完成。
