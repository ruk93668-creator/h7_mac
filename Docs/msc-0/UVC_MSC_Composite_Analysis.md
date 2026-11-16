> 基线 `f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb`；2026-11-16。
> VERIFIED：源码确认；INFERRED：源码推断/拟议设计；UNRESOLVED：待确认；NOT_TESTED：未实测。以下新架构均未实现。

# UVC + MSC 复合设备可行性

## 资源事实与候选分配

VERIFIED：`usb_uvc_desc.c`设备EF/02/01、IAD覆盖VC0/VS1；VS Alt0无EP，Alt1 EP81 ISO MPS1023、interval1；无VC interrupt EP。描述符按YUY2/JPEG配置生成，有独立静态存储及PIO尾部。`usb_uvc_class.c`固定pUserData[0]、接口0/1及EP1，不能把第二个Class直接注册替换它。

|资源|当前 VERIFIED|候选 INFERRED|
|---|---|---|
|interface0|UVC VC|保留|
|interface1 alt0/1|UVC VS|保留|
|interface2 alt0|不存在|MSC 08/06/50、两个Bulk|
|EP0|共用控制|标准设备请求统一；interface与endpoint请求分发|
|EP81|UVC ISO IN|保留|
|EP82/02|未被当前UVC使用|MSC Bulk IN/OUT，MPS64|
|IAD|UVC 0/1|保持范围，不包含MSC|
|配置描述符|当前格式动态生成|复制/组合有效UVC部分并追加MSC，更新total length和接口数量；不得改VC/VS内部长度和引用|

VERIFIED：YUY2配置180字节，`UvcDesc_SelectMjpeg`生成MJPEG配置158字节（VS total length55），GetFSConfigDescriptor返回当前格式长度；复合构建不能固定假定180。UNRESOLVED：设备VID/PID和字符串绑定需另行规划。

## Core 与生命周期

VERIFIED：当前构建Class上限1，Platform单生命周期、唯一1024字节分配块、usbd_conf接口上限只覆盖编号0/1。中间件composite宏所需builder不在树中；EP0_TxSent固定Class0，ISO incomplete依classId，单纯增加Class数组不够。

INFERRED：优先评估单个外层Composite Class注册到现有Core，在Setup按recipient和完整wIndex路由，DataIn/Out按完整端点地址路由；保存EP0事务owner/token，遇新Setup取消旧控制事务；SOF和ISO incomplete仅交UVC，Bulk完成仅交MSC。另一条路线是补齐ST native composite builder并改Platform为多Class，但范围更大，不作为直接开宏方案。

INFERRED：外层dispatcher仍需解决UVC的pUserData[0]与分配器独占：MSC持久IO状态放独立静态上下文，不争用UVC pool，或经审批重构成显式每Class上下文。不能用未保护的全局指针临时切换伪造并发。全局Init任一Class失败须逆序释放已成功资源；失败保留占用并锁故障；DeInit必须隔离MSC worker在途buffer。MSC持久owner不能跟随USB Class内存被清空。

INFERRED：主机SetConfiguration/总线Reset是全局事件；UVC Alt0仅停UVC81，MSC eject仅令LUN not-ready，不停USB或UVC；BOT Reset只作用MSC且保持halt/toggle，不能走全局USB_Mode_Stop。独立业务start/stop需新增控制入口；现有mode只允许同模式重连，其全局Stop会释放全部数据EP，不可当Class独立停止。

## FIFO 与带宽

VERIFIED：`bsp_usb.c:15-18,854`使用软件总预算1024 words，RX320、TX0=64、IN1..8最少各16；按配置所有alt的最大IN MPS分配向上取整word。当前UVC EP1需要ceil(1023/4)=256。

INFERRED：按现有规划公式，UVC+MSC为320+64+256+7×16=752 words（3008字节），余额272 words。EP2 Bulk64需要16 words，已包含最低占位，因此无额外增量；OUT共用RX320。仅MSC为512 words。此为软件预算，不等于硬件验收；必须复核芯片revision的端点/FIFO能力和实际寄存器。RX规划没有根据同时OUT高峰动态扩容的证据，需要实测EP0与MSC OUT共存和Abort/Flush。

VERIFIED：`USB_UVC_ValidateFSConfig`仅检查ceil(frame_bytes/1021) <= floor(1000/fps)，每SOF最多一包，包含2字节头；没有为MSC保留带宽，也没有证明总线事务开销预算。BSP PIO多包IN由IRQ复制推进，UVC Task构包期间还屏蔽USB IRQ。

INFERRED：FS原始12Mbit/s不能当作MSC吞吐承诺；UVC单包最大视频payload上界1021×1000=1,021,000 B/s，尚未计总线开销和实际缺槽。周期ISO占用优先、无重传，Bulk使用剩余调度且可能被NAK/饥饿。即使平均JPEG较小，worst-case帧和burst仍需预算；不能只用平均FPS判断。MSC worker阻塞时间、CPU copy/IRQ延迟、SDRAM总线争用均需纳入。

NOT_TESTED：每种UVC格式/尺寸/fps与MSC读、写、交替负载联合运行；记录EOF实际FPS、丢包/Incomplete、SOF gap、MSC吞吐/p99延迟、SD超时、FIFO寄存器与任务栈；注入BOT reset、bus reset、Alt切换、SD故障与断线。联合验收阈值必须预先由产品确定。可能需限制UVC最高档位或MSC速率；当前结论为架构可设计、稳定并发尚未证明。
