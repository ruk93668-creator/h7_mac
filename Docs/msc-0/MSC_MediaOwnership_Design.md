> 基线 `f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb`；2026-11-16。
> VERIFIED：源码确认；INFERRED：源码推断/拟议设计；UNRESOLVED：待确认；NOT_TESTED：未实测。以下新架构均未实现。

# FAT 卷所有权设计

## 已有保护与缺口

VERIFIED：`sd_block.c`绑定Task+FATFS/RAW租约；`StorageFS_Mount`取得FATFS租约；`StorageFS_Unmount`要求所有文件关闭、SD_Block_Sync成功并释放租约。FatFS不重入，卸载不自动flush。`app_storage.c`会写AVI、回写头和索引；停止前必须结束完整业务。

INFERRED：这些机制足以作为底层互斥基础，不能证明PC缓存已提交，也没有跨USB Reset的所有权记录。新增协调者只设计不实施；USB生命周期与介质授权分开。

## 状态与强制门禁（INFERRED）

```
MCU_OWNED → QUIESCING → PC_OWNED → RETURNING → MCU_OWNED
                   ↘ 失败               ↘ 不可证明
                    UNCERTAIN_LOCKED ← 异常断开/写失败
任何状态拔卡 → NO_MEDIA（保留旧卷不安全记录）
```

MCU_OWNED：仅StorageTask持FATFS租约，MSC ready=false；身份/容量查询不可绕过SD_Block任务限制。QUIESCING：锁住新的本地录制/文件命令，等待现有业务结束；归还相机/AVI frame lease；AVI尾部/索引完成，逐文件Sync和Close，确认open_files0，StorageFS_Unmount成功，SD租约释放。随后在同一worker Claim RAW、Init/GetInfo/GetIdentity，检查无active/fault并记录generation，才公布PC ready。

任一步失败均不向PC暴露介质。若能完整证明本地卷仍有效且没有解除/部分移交，可回MCU_OWNED；否则UNCERTAIN_LOCKED。不能因为Unmount返回BUSY而强制丢弃文件，不能将StorageFS_Recover的失效句柄操作当成成功同步。

PC_OWNED：本地FatFS所有入口（包括后台日志、AVI、状态诊断触发挂载）禁止；请求MCU重新使用时只置pending，不抢占RAW租约。PC块IO由worker串行执行。USB重新枚举仍检查owner，不能自动挂载或同时重新暴露同卷。

RETURNING：仅接收到经过校验并允许的逻辑eject才开始；拒新媒体写入，等待USB OUT、队列、同步SD写入/卡忙完成；Sync并完成eject CSW，确认Bulk IN完整传输完成。LUN保持ejected/not-ready，USB可继续枚举以响应查询，禁止host load自动绕过仲裁。确认所有buffer无持有、active/fault0和generation匹配后，RAW Release→StorageFS_Mount由原StorageTask执行。挂载成功才MCU_OWNED；挂载失败保持锁定/故障，不将ready交给任一方。

PREVENT置位时逻辑eject失败，不能先释放租约再回应。SYNCHRONIZE CACHE只同步已到达设备的写入；不代表主机不会再写。NOT_TESTED：Windows安全移除是否在当前驱动组合实际发送START STOP、何时完成CSW；若没有可验证eject证据，采用保守锁定，不假定OS UI动作必达设备。

## 事件与归还判定

|事件|INFERRED 规则|允许MCU挂载？|
|---|---|---|
|PC正常读写|保持PC owner；本地请求排队|否|
|PC写中拔线/无安全弹出|隔离USB，epoch失效；等待或终止设备在途IO，记录可能部分写入→UNCERTAIN_LOCKED|否|
|只有disconnect|不证明主机缓存落盘；没有已完成eject证据则锁定|否|
|USB bus reset|只重建协议状态；取消旧CBW/数据/CSW，旧SD请求隔离，保留PC owner/故障锁|否|
|Suspend/Resume|不当eject，不归还卷；恢复后继续受原owner约束|否|
|eject但DMA/卡忙未结束|RETURNING，等待有界；超时锁定|否|
|eject成功且CSW完成|LUN锁住；排空+sync+无持有+释放RAW后尝试mount|仅挂载成功后|
|CSW提交成功但未完成/此时掉线|不能证明主机收到了归还完成；保守锁定|否|
|SD错误/超时/Abort失败|保留可能部分写入、缓冲占用和fault；worker显式恢复不等于FAT修复|否|
|Unmount失败/文件未关|不交PC，不强制清句柄；按是否能证明本地完整性决定保留或锁定|不自动重新开放|
|PC_OWNED时MCU请求|通知用户需要PC归还；不能通过USB_Mode_Stop抢卡|否|
|重新插卡|新generation/CID重识别；不继承旧ready或归还标志|需新介质授权和检查|
|重启/掉电|RAM owner丢失不是安全归还；启动策略必须保守|不能凭重启自动挂载|

安全归还谓词：显式eject证据 ∧ 请求/CSW对应当前epoch和介质 ∧ 无新媒体命令可接受 ∧ 已接受写全部完成 ∧ SD Sync成功 ∧ USB/SD/CPU无buffer持有 ∧ RAW释放成功。随后本地mount还可能因PC格式化/损坏而失败。设备无法证明或补回PC未发送的数据；不得承诺掉电事务原子性。

## 待决策与测试

UNRESOLVED：跨重启如何保存dirty/ownership状态。候选为卷外持久记录并带CRC/序号/原子更新，或默认禁止自动挂载；需另行评审写入位置、磨损、掉电窗口。不能擅改FAT保留扇区。人工点“确认”本身不是安全证明，应要求主机检查/修复和明确归还流程；风险恢复权限与正常安全归还分开。

NOT_TESTED：逐状态注入reset/disconnect/suspend、Unmount/Sync错误、晚回调、队列满、PC写时MCU请求；每项要求检查所有FatFS入口均拒绝、无抢卡/提前释放、旧token不作用于新卡。所有权转换日志要包含old/new owner、epoch、generation、结果和active/buffer数量。
