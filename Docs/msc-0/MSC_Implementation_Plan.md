> 基线 `f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb`；2026-11-16。
> VERIFIED：源码确认；INFERRED：源码推断/拟议设计；UNRESOLVED：待确认；NOT_TESTED：未实测。以下新架构均未实现。

# MSC-1～MSC-7 实施计划

INFERRED：以下路径为拟新增，现有修改点已从基线确认。每阶段独立提交与回归，不把本报告当作实现授权；MSC-0后停止。后续是否改Core/Keil/UVC必须在阶段授权范围明确。

## MSC-1：独立MSC枚举

- 文件：新增Service/usb_msc_desc、usb_msc_class；修改Service/usb_mode与Application USB启动接入；Keil新增文件需单独批准。
- 接口：USB_MSC_Register；Class Init/DeInit/Setup，固定LUN0但介质not-ready；不接生产SD。
- 测试：Windows描述符/设备管理器、FS MPS、GET_MAX_LUN、无数据在途Reset、重复配置/取消配置、非法Setup、TEST与原UVC构建回归。
- 验收：08/06/50及双Bulk正确枚举，无SD访问，无句柄/端点泄漏。未完成BOT前不得宣称可用磁盘。
- 依赖：MSC-0审核；回滚：撤销阶段提交/关闭MSC构建入口，恢复原模式；不改已有UVC实现。

## MSC-2：BOT与SCSI（RAM LUN）

- 文件：新增Service/usb_msc_bot、usb_msc_scsi、测试RAM adapter；Platform/usb_device需评审增加延后EP0状态支持，若选Core改法需单独批准。
- 接口：BotEvent/Process/Reset、ScsiDecode/Execute、LUN geometry/status/read/write/sync completion；token/epoch取消和状态阶段续接。
- 测试：13-case、invalid/meaningless CBW、CSW tag/residue、sense、所有命令表、MPS/短包/ZLP、重复clear halt与Reset顺序、DataInError、控制事务被新Setup取消。
- 验收：RAM读写一致，所有错误无伪成功；Reset在途异步安全，不在ISR阻塞；Windows命令抓包解释清楚。
- 依赖：MSC-1；回滚：恢复无介质枚举，独立撤销协议提交，RAM数据可丢弃。

## MSC-3：Block Adapter与任务契约

- 文件：新增Platform/msc_block_adapter，扩展Application/Tasks/storage_task与app_storage消息处理，保持sd_block/BSP算法不变为首选。
- 接口：geometry/present/protected、异步请求与完成、media generation、timeout、sync、cancel/drain；底层worker独占RAW租约。
- 测试：mock读写故障/部分完成、越界/溢出、队列满、buffer状态、旧epoch完成、新介质、Claim/Release拒绝条件。
- 验收：USB路径不阻塞，单CBW/worker IO，所有buffer释放可追踪，FatFS与RAW无法同时Claim。
- 依赖：MSC-2；回滚：恢复RAM adapter，撤销worker接入。

## MSC-4：SD真实读写

- 文件：接通msc_block_adapter→现有SD_Block API；仅必要诊断改动需单独说明，不重写SDMMC。测试构建完全禁止本地FatFS访问测试卡。
- 接口：绑定GetInfo/Identity/Status、Read/Write/Sync；错误保留SysResult与completed_blocks。
- 测试：专用可销毁卡，先只读；512/多扇区/32KiB边界/非32KiB尾块、LBA0/末尾、随机校验，再写入；IDMA fault/卡忙/拔卡/Abort失败。
- 验收：PC扇区内容一致，越界不下发、失败无伪成功，MPU/地址/active与卡状态证据齐全。未完成MSC-5前禁止生产卡/自动挂载。
- 依赖：MSC-3；回滚：恢复RAM LUN，故障测试卡离线检查，固件回滚不修复文件系统。

## MSC-5：FAT所有权

- 文件：新增Application/msc_media_owner；修改app_storage、storage_task、USB MSC媒体状态接入；StorageFS仅在必要门禁/诊断范围审查。
- 接口：RequestPc/RequestMcu、QuiesceAck、Eject/ReturnAck、Locked、DirtyBootPolicy；既有AVI收尾/句柄同步/Unmount显式确认。
- 测试：全部状态事件矩阵，未关文件、AVI结束失败、Sync/Unmount失败、PC写时MCU请求、eject中断、重启/断线/Reset/Suspend、全入口旁路检查。
- 验收：无安全证明不挂载；所有权与任务租约一致；断线不自动归还；持久状态或保守启动政策经评审并验证。
- 依赖：MSC-4与所有权设计审核；回滚：禁用SD暴露并保持保守锁定，不能简单回退成自动挂载固件。

## MSC-6：异常与稳定性

- 文件：Tests新增MSC协议/故障/长稳脚本，必要BOT/owner/adapter修复及诊断；生产修复各自小提交。
- 接口：只读计数、错误注入（测试构建）、buffer/epoch/owner快照。
- 测试：长时间循环/大文件/随机块，bus reset与BOT reset区别、拔线/掉电/热插拔、PC未安全移除、连续恢复、资源泄漏和任务栈，PC离线文件系统校验。
- 验收：时长/轮数/吞吐及延迟阈值先约定；数据校验通过、无死锁/越权/过早释放；不能将设备已收到数据正确写入扩大为保证PC未发送数据不丢。
- 依赖：MSC-5；回滚：独立撤销具体修复，保留故障诊断与卷锁，不清故障数据。

## MSC-7：复合与联合验收

- 文件：新增Service/usb_composite_class/desc；usbd_conf接口上限调整、USB应用调度和分配/上下文隔离需审批；若涉及UVC/Core修改先明确范围。
- 接口：IF/EP路由、EP0 owner、独立Class停止、全局reset协调、带宽档位策略。
- 测试：UVC各档位×MSC读/写联合长稳、独立Alt0/eject/BOT reset、全局reset、FIFO共享OUT取消、SOF gap/Incomplete/帧率/MSC延迟。
- 验收：752word软件预算经芯片及寄存器核对；满足预先约定的双负载指标，UVC帧与MSC介质所有权各自独立；单独枚举不是联合验收。
- 依赖：MSC-6及资源/带宽实测；回滚：恢复独立MSC或原UVC构建、保存介质owner/故障状态。

## 当前交付检查

VERIFIED：本阶段只写docs/msc-0内指定七份Markdown；已有草案正文保留为历史记录。无生产/工程/链接/MPU/RTOS/M4改动，不执行clean、不合并main。

NOT_TESTED：本阶段未编译、烧录、运行Windows或板级传输；文档检查与Git差异检查不代表上述阶段测试通过。所有UNRESOLVED项均为后续评审/实施门槛，不能自行跳过。
