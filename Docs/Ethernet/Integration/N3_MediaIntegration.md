# 正式媒体消费者接入审查

状态 SOFTWARE_BLOCKED。

Common/Inc/camera_config.h 已有 CAMERA_FRAME_CONSUMER_ETHERNET，Media FIFO 的借用接口可按消费者保存独立身份。正式 Camera Service 维护 s_transfers[CAMERA_FRAME_CONSUMER_COUNT]，并在 Camera Task 内完成 acquire 和 release。这是复用的基础。

当前真实路由仅支持 USB 与 Storage。CameraFrame_Send 在 Camera Service:103–109 拒绝其他消费者。CameraService_AdvanceFrames:311–323 只读取两条上行通道。App_Messages 的发送者、接收者、允许类型和队列绑定也只含这两路。不能将 E7 Provider 直接装入 NetworkTask，因为其直接调用 CameraCapture 的 Get/Release 接口。

正式 Network 消费者必须通过新增合法双向通道复用现有 FRAME_REQUEST → FRAME_OFFER → FRAME_RETURN → RETURN_ACK → RETURN_ACK_CONSUMED 协议。队列满时保留原 command_id、stream_epoch 和 lease_id。RETURN 入队仅表示请求提交，RETURN_ACK 表示 Camera 所有者已经执行物理归还，最后消费确认使交接记录退休。

私有 JPEG buffer 由 Network 独占，保留 E7 的跨尾复制与 8 字节分段读取。完成复制后请求归还 Camera 帧；私有 JPEG 在所有内容复制进 TCP 应用环后才归还，ACK 只驱动网络进度。停止、重启或相机重新配置不能丢弃尚未闭合的消息身份。视频禁用时不申请帧，Raw 模式拒绝 MJPEG。

本轮未扩展消息路由或生产 Provider，因此全部多消费者组合、队列满、重复归还、旧 Epoch、浏览器断开和重新配置专项均为 NOT_RUN。
