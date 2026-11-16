> 审计日期：2026-11-16；源码基线：`f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb`；分支：`codex/usb-msc-bringup`。
> VERIFIED = 已由当前源码确认；INFERRED = 源码推导或拟议设计，尚未实现；UNRESOLVED = 仍需确认；NOT_TESTED = 未实测。源码事实不等于硬件验收。

# SDMMC、FatFS 与 DMA 安全审计

## 可复用性结论

VERIFIED：存在独立 `CM7/Platform/sd_block.h/.c`，API为 Claim/Release/IsOwner/GetStatus/GetIdentity/Init/Read/Write/GetInfo/Sync/Recover。Read/Write接受32位LBA、扇区数量、调用者buffer和毫秒超时；512字节逻辑扇区。它可作为MSC底层，不能从USB ISR直接调用，不能绕过租约调用BSP。MSC使用RAW租约，不能复用FATFS租约或调用diskio。

INFERRED：建议保留StorageTask为唯一块设备执行任务（本地文件和RAW模式串行），MSC通过带epoch的请求/完成队列接入；这可避免新增Task抢占当前租约。也可移交给独立worker，但现有租约绑定TaskHandle，必须先由原任务Release再由新任务Claim。任务/块租约不等于PC卷安全所有权。

## 实现证据与调用链

|文件与定位|结构/函数|VERIFIED 行为及上下文|
|---|---|---|
|`CM7/BSP/bsp_sdmmc.h:83`|`BspSdmmcCardInfo_t/IoDiagnostics_t/WaitOps_t`，DMA常量|BASE=0x24000000，BYTES=32768，BLOCK_SIZE=512，DMA_BLOCKS=64；capacity_bytes为64位，block_count为32位|
|`CM7/BSP/bsp_sdmmc.c:140`|`BSP_SDMMC_Init`、`Sdmmc_Clock/ConfigValid`|检测卡→配置时钟→HAL_SD_Init低速1bit→验证逻辑块长512/数量非0及类型→CMD13 TRANSFER；缓存容量，不能以GetCardInfo代替实时插卡|
|同文件`:414`|`BSP_SDMMC_EnableTransferMode`|检查MPU/等待环境；低速切4bit，合适卡协商High Speed；240MHz内核分频5或3，对应24/40MHz，验证寄存器和CMD13；IRQ7|
|同文件`:509,1083`|`BSP_SDMMC_ReadBlocks/WriteBlocks`→`Sdmmc_Transfer`|统一截止时间；减法校验LBA范围、防字节乘法溢出，单操作busy锁；每批最多64扇区/32KiB；写先复制到AXI，读完成后复制给调用者|
|同文件`:1008`|`Sdmmc_WaitBatch`、`Sdmmc_Quiescent`|检测存在/超时/错误；要求HAL完成标志、IDMA关闭、命令/数据路径静止、HAL READY、DCOUNT=0、CMD13 TRANSFER，成功才可重用buffer|
|同文件`:601,966`|`SDMMC1_IRQHandler`→notify；Task `Sdmmc_ServiceEvents`→HAL_SD_IRQHandler→Rx/Tx/Error回调|实际IRQ仅锁存终止状态、屏蔽MASK并唤醒；HAL IRQ函数在任务服务循环执行。不能因函数名把HAL_SD_TxCpltCallback判为本工程ISR上下文|
|同文件`:1054,559`|`Sdmmc_StopIo`、`BSP_SDMMC_Recover`|失败Abort并验证quiescent，fault锁存；Recover显式外设reset/cleanup并清旧卡信息，之后需重新Init；失败可能保留active，不伪造释放|
|同文件`:670`|`Sdmmc_Take/Give`|短PRIMASK保护busy，是拒绝并发的门禁，不是排队互斥锁；直接BSP API不校验逻辑客户身份|
|`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_sd.c:1235,1333`及Inc|`HAL_SD_ReadBlocks_DMA/WriteBlocks_DMA`|IDMABASE0=传入buffer，IDMACTRL为single buffer；当前BSP始终传g_sdmmc_dma_buffer，不直接DMA调用者buffer|
|`.../stm32h7xx_hal_sd_ex.c`、`stm32h7xx_ll_sdmmc.c`及Inc|`HAL_SD_ConfigSpeedBusOperation`、`SDMMC_Init`及命令函数|BSP速率/命令依赖；不提供卷所有权、文件锁或MSC适配|
|`CM7/Platform/sd_block.c:41,72,207`|`s_owner`、`s_client`、Claim/Release、`SdBlock_CanWait/Wait/Notify`|唯一Task+FATFS/RAW身份；拒ISR、PRIMASK/BASEPRI非0、调度器未RUNNING；静态二值信号量等待，中断GiveFromISR；Release拒active/fault|
|同文件`:141`|`SD_Block_Sync`|只检查存在、初始化、fault、busy；没有写队列，也不另发cache flush。其语义建立于Write同步等卡TRANSFER，不代表SD内部掉电原子性或主机缓存已落盘|
|`CM7/Platform/diskio.c:142,155,176,231`、`fatfs_diskio.h`|disk_read/write/ioctl、`Disk_Check/Result`、VolToPart|仅FATFS owner；读2000ms/写3000ms；绝对LBA，分区偏移由FatFS计算一次；写受s_first/s_count及writable限制；CTRL_SYNC→SD_Block_Sync；错误锁存|
|`CM7/Platform/storage_fs.c:86,189,226`、`.h`|`StorageSlot_t`、静态FATFS/FIL槽、Mount/Unmount/Recover|Claim FATFS→识卡/CID/检查FAT布局→配置diskio→f_mount即时加载；Unmount要求open_files=0、SD Sync成功才注销/Reset/Release；Recover使句柄失效，不是数据修复|
|同文件`:333,403,418,561`|Write/Sync/Close、`Storage_Enter/Finish`|进入检查owner/state/busy/卡状态；Write经8192字节s_write_input→f_write，返回实际进度；Sync=f_sync；Close=f_close成功才减句柄；介质错误使会话fault并保留租约|
|`CM7/Platform/ffconf.h`|FF_FS_REENTRANT=0、LOCK=4、VOLUMES=1、MIN/MAX_SS=512、LBA64=0|FatFS不重入；4文件锁不等于跨任务互斥；不支持exFAT/LFN/GPT等当前配置能力，PC修改格式后本地可能不能挂载|
|`Middlewares/Third_Party/FatFs/ff.c:1113,3744,4223,4304`、`ff.h/diskio.h`|sync_fs/f_mount/f_sync/f_close，FATFS/FIL|sync_fs最终CTRL_SYNC；f_close调用f_sync；f_mount(NULL,opt0)只注销/使对象无效，没有自动flush；有效卷路径时返回FR_OK|
|`CM7/Application/Tasks/storage_task.c:31`、`app_storage.c:649,710,931`及头文件|StorageTask_Run→App_StoragePrepare/Process；Storage cleanup|StorageTask串行驱动文件API、AVI写入/关闭/卸载、故障显式恢复；当前没有PC所有权协调状态机|
|`CM7/Service/mjpeg_avi.c`|AVI写头/索引、结束回写和StorageFS_Sync|调用StorageFS而非裸diskio；QUIESCING须完成AVI封装，不只是close文件|

VERIFIED：搜索CM7当前调用点，FatFS f_*生产调用集中StorageFS，另有diskio；应用文件访问集中App_Storage及其同步调用的mjpeg_avi。当前有效路径由StorageTask驱动；这不能保证未来新增代码不会绕过门禁。

## 历史参数核验与 DMA/Cache/Buffer

- VERIFIED：32KiB确为本基线实际定义；`CM7/BSP/bsp_sdmmc_memory.c:7` `.sdmmc_dma`、zero_init、aligned(32)。`MDK-ARM/h7_mac_CM7_custom.sct:25` 独占0x24000000..0x24007fff UNINIT；普通RW/ZI在DTCM 0x20000000..0x2001ffff；Keil CM7 ScatterFile确指向此脚本。
- VERIFIED：`BSP_SDMMC_MemoryInit:22` 在DCache未启用时才配置MPU region3：32KiB、XN、TEX1、shareable、non-cacheable、non-bufferable；`MemoryValid:56`核对实际地址/大小/MPU寄存器，传输前复查。
- VERIFIED：8KiB有两个具体含义：StorageFS写输入暂存为8192；BSP对SDRAM源复制到AXI以8192分段，默认段间1ms。IDMA批次本身上限是32KiB，并非8KiB。内部RAM源不走同样的SDRAM分段间隔。
- VERIFIED：`Sdmmc_CpuBufferValid:801`允许CPU buffer在DTCM、排除专用窗口后的AXI、D2 SRAM、SDRAM，写源另可Flash；禁止调用者直接传DMA专用区。调用者buffer对齐不等于DMA对齐；DMA始终访问32对齐AXI staging。
- VERIFIED：当前SD路径不对调用者buffer执行Clean/Invalidate，CPU copy跨接noncache DMA区；USB DMA关闭，PCD PIO由CPU访问USB buffer。因此当前设计不能被描述为“USB DMA与SD IDMA共享零拷贝”。
- INFERRED：MSC独立staging放CPU可达普通RAM，读：IDMA→AXI→CPU copy→MSC buffer→USB PIO；写反向。即便普通buffer位于DTCM，当前PIO/CPU路径可工作，未来开启USB DMA必须重审地址可达性和Cache。
- INFERRED：建议32对齐、整扇区staging，32位PIO尾部预留；每buffer FREE/USB_RX/WORKER_WRITE/WORKER_READ/USB_TX/CANCEL_PENDING，只有完成或确认abort静止才回收。读USB IN源直到整提交最后包完成仍被BSP引用；写OUT completion后buffer才可交worker，worker同步Write返回后才可重arm。
- INFERRED：若未来直接DMA cacheable RAM，必须按真实cache line隔离、DMA读前Clean、DMA写前处理dirty line/写后Invalidate和屏障，严禁invalidate邻接脏对象。此为未来约束，本阶段不修改MPU/cache。
- UNRESOLVED：没有本次构建map和实板寄存器快照，不能把scatter意图提升为实机已验证布局；整机DCache实际启用状态、启动顺序及SD卡掉电缓存能力尚未实测。BSP MPU初始化失败被initcall吞为SYS_OK，实际靠MemoryValid拒绝传输；应暴露诊断，不能忽略。
- NOT_TESTED：读写超时边界、漏IRQ、Abort失败、热拔卡、缓存开关、PC并发与长时间运行。

## 审计文件清单补充（VERIFIED）

以下为当前树实际文件与行数；用于核对扫描范围，不能替代上文行为证据。HAL/LL和FatFS实现另见上文对应路径。

|路径|行数|
|---|---|
|`CM7/Application/app_storage.c`|1026|
|`CM7/Application/app_storage.h`|106|
|`CM7/Application/Tasks/storage_task.c`|64|
|`CM7/Application/Tasks/storage_task.h`|7|
|`CM7/BSP/bsp_sdmmc.c`|1212|
|`CM7/BSP/bsp_sdmmc.h`|151|
|`CM7/BSP/bsp_sdmmc_hal_config.h`|14|
|`CM7/BSP/bsp_sdmmc_memory.c`|87|
|`CM7/Platform/diskio.c`|249|
|`CM7/Platform/fatfs_diskio.h`|23|
|`CM7/Platform/ffconf.h`|48|
|`CM7/Platform/sd_block.c`|273|
|`CM7/Platform/sd_block.h`|56|
|`CM7/Platform/storage_fs.c`|782|
|`CM7/Platform/storage_fs.h`|90|
