# MSC-2 BOT / SCSI / RAM LUN

VERIFIED：保留单Class注册，FS Bulk 0x82/0x02、64B。新增BOT、SCSI、后端契约及16×512B RAM LUN；SCSI只生成计划。默认生产启动仍为UVC；测试构建显式USB_MODE_BOOT_MSC=1。Keil仅向CM7 Service组追加三个文件，用户其余修改经SHA256核对未变。

## VERIFIED：协议与所有权

31B CBW显式小端解析，校验signature、flags、LUN=0、CDB长度1～16；非法wrapper双向STALL并等待Reset。13B CSW包含signature/tag/residue/status。13-case纯函数结合端点状态机，Case13拒绝不足长度的OUT，不执行部分写入；未知命令返回ILLEGAL REQUEST。

实现INQUIRY、TUR、REQUEST SENSE、READ CAPACITY10、READ/WRITE10、MODE SENSE6、START STOP、PREVENT ALLOW及SYNC CACHE10。检查保留字段、长度、零块、减法边界及写保护；FUA、EVPD等未支持字段返回错误。缓存页WCE=0，代次改变产生Unit Attention。PREVENT/LOAD/SENSE副作用在成功CSW真实完成后提交。

ISR仅登记OUT事件；UsbTask推进SCSI、RAM复制及后端请求。1024B、32字节对齐BOT缓冲，USB PIO与后端交替独占。Platform完成序号确认IN/CSW，CSW实际完成前不接收下一CBW。Case7完整读取底层扇区，只发送主机约定长度。

BOT Reset通过Abort保留HALT，cancel后端，任务等待双端点及后端排空再提交EP0；状态真正完成并收到双向Clear HALT才重收CBW。超时进入锁定故障。Bus Reset/Disconnect/DeInit取消Class，新Init仍等待旧epoch排空。身份计数耗尽拒绝新事务。

Platform新增最近一次控制完成Token的只读历史查询，解决任务运行前连续Clear HALT。新SETUP仍使旧资格CANCELLED，历史证据不允许再次提交。

## VERIFIED：测试

Tests/MscProtocol/results.json：32场景通过，真实Core/Platform/BSP/MSC、Mock HAL。覆盖全部13-case、非法CBW、双扇区RAM写读、Reset立即Clear HALT、取消新SETUP、Abort失败超时、Bus Reset及SCSI边界。EP0专项17场景通过。

build_results.json：MSC/UVC完整编译均0 Error、0 Warning。MSC Code143954/RW1528/ZI110600；UVC Code142254/RW1364/ZI102316字节。RAM LUN在UVC链接中裁剪。USB/Storage任务既有静态栈各2048 words未修改。

NOT_TESTED：Windows真实BOT/RAM读写、USBPcap、硬件取消、吞吐、长时间运行及栈高水位。软件交付不代表MSC-2实板验收完成；历史UVC失败按EP0基线对照保留。
