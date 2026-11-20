# P1-C3/C4：MSC消费者防护与剩余迁移

VERIFIED：本轮新增USB_MSC_BotBackendDrained，只查询当前backend旧epoch。Msc_DeInit先关闭接口继续提交资格、BotStop请求cancel，再查询drained；尚未drained返回USBD_BUSY，不执行任一Close，不把cancel请求当成完成。NULL backend只表示从未借给Worker；缺drained函数则保守拒绝。

VERIFIED：三项专项链接真实Class/BOT/Storage mailbox，覆盖运行中DRAIN、ISR内有界DeInit查询、QUEUED尚未执行的取消。device.quiescent=false时DeInit保持BUSY且Close计数为0；只有该硬件边界输入变为quiet、真实StorageProcess把请求退休后才允许尝试Close。USB Close桩始终返回BUSY，验证Class仍不报告成功，不模拟USB proof。源哈希、每步状态、断言和初轮失败见Tests/UsbLifecycleFinal/reports/worker*.json。

INFERRED：目标仍应为同一Coordinator intent引用IN/OUT独立Attempt及ConsumerTicket；BOT Reset在ISR仅登记、撤销旧CBW资格，Task锁外推进endpoint和worker，Deferred Status在两类proof及Token均匹配后提交。Clear HALT须等实际状态完成，才允许新CBW；不能重置epoch伪造drained。

BLOCKED：未执行BOT Reset的完整事务迁移。当前usb_msc_bot.c的USB_MSC_BotReset仍调用同步Abort，BotProcess整段USB_Device_Lock范围不适合直接嵌入新的分阶段推进。P1-E ZLP/EP0票据门禁未闭合，故不在已有Class请求中自动Enable C1，也不把只完成消费者查询守卫宣布为BOT联合验收成功。

VERIFIED：RAM与SD backend均通过同一backend.drained契约；StorageTask/SDMMC/FatFS/RAW租约实现未改，SD写入未打开。USB端点提交成功、Token失效、Reset均不是Storage静止。NOT_TESTED：真实IDMA、实板BOT Reset/重连及SD读写，本轮不释放RAW给FatFS。
