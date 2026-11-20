# P1-G：板级验收准备（禁止本轮执行）

BLOCKED：当前没有完整EP0/消费者/ISO/Controller重启证明，不具备P1联合实板执行或main合并资格。NOT_TESTED：本轮全部实板项。本文和工具只作准备，不授权烧录、Reset、Break、抓取活动USB、拔线或发送真实SD写入请求；实际执行须单独批准且先关闭软件门禁。

## 执行前门禁

1. 核对批准的软件SHA、实际AXF/HEX/MAP哈希、ARMCC版本、编译选项和生产调用交叉引用；不得烧录旧别名。
2. 核对具体芯片型号、当前REV_ID和core ID；历史0x2003只代表U/V组，不区分U与V，也不是本轮读取值。
3. RAM阶段断电取出SD；SD只读阶段断电插卡，WRITE_ENABLE最终预处理值必须0，保持原CM4。确认RAW Claim和FatFS拒绝，绝不格式化、修复或写卡。
4. 捕获必须先于连接/Reset开始，同一电脑、线缆、USB端口。原始pcap完整保留，明确bus/address及每次Session映射。未知Hub其他设备的数据不上传。
5. 确认EP0/ISO/Worker/RAW所有未闭合条件获审，不把模型Reset当拔线；任何资源仍owned即拒绝新模式/新Session。

## 顺序与判据

| 组 | 操作（未来批准后） | 必须保存与判据 |
| --- | --- | --- |
| RAM-0 | 冷启动枚举、INQUIRY/Capacity/Mode Sense | Device/Configuration描述符、SET_ADDRESS/CONFIGURATION、CBW/CSW Tag/Residue、真实Control Token；状态包提交与完成区分 |
| RAM-1 | 受控RAM LBA读写与校验 | 确认为RAM backend且无SD卡；分块、完成数、哈希一致，不能把RAM正确当SD通过 |
| RAM-2 | 正常插拔/Reset/BOT Reset/Clear HALT及连续10次重连 | 每次完整抓包、旧StopIntent/Attempt/Session/NAK lease、所有endpoint和消费者ledger；旧回调不驱动新CBW，真实重新枚举及再READ |
| RAM-3 | IN/OUT活跃时断开，XFRC/EPDISD/Reset交错 | 不提前归还Buffer，IRQ仍能收尾；迟到事件归旧身份；失败进入RECOVERY_REQUIRED且不自动重启绕过 |
| SD-0 | 只读初始化与Windows容量/FAT读取 | CID、512B扇区、RAW租约、FatFS拒绝、IDMA诊断；文件复制到PC；独立原件SHA不存在则原始端到端正确性NOT_TESTED |
| SD-1 | 连续/跨BOT块/尾部LBA读取及重连 | USB endpoint proof和SD IDMA quiescent分别记录；写入块数0、WRITE入口计数0；不因断线转交FatFS |
| UVC-0 | 原视频、Probe/Commit、Alt1→Alt0→Alt1 | 描述符不变；活动ISO真实XFRC/Incomplete/Disable/FIFO、Frame/Bridge Return/ACK对应，消费者全部退出才归还Camera |
| UVC-1 | SOF/Stop/Reset/Disconnect及长时间运行 | 不虚构ISO逐包ACK；无重复Frame归还、不在停止时再次发送，不以可独立枚举推断联合生命周期通过 |
| END | Fault现场保存、安全退出及断电 | 没有在途IDMA/PIO；未知卷归还状态保持禁止FatFS；完全断电后取卡 |

## 离线工具（已实现，不操作硬件）

`Tests/UsbLifecycleFinal/hardware_tools.py`：

- prepare：读取最终ELF真实符号及尺寸，重复/缺失符号标UNRESOLVED，不猜地址；生成只含SAVE的Keil脚本和固件哈希manifest。单独列出的寄存器只读；绝不包含GRXSTSP或DFIFO。脚本本轮不执行。
- snapshots：校验Intel HEX checksum、EOF、地址全集与manifest匹配；只导出原始bytes/words，不把Break快照当完整事件时序或自动PROVEN。
- pcap：仅通过tshark -r读取已存在文件，要求显式设备过滤器；校验格式、保存SHA/原始JSON/stderr和观察统计。计数、Tag出现不等于协议通过，不自动配对跨Session复用Tag，也不自动宣告重连PASS。

输出只能是output/MSC24/udlm-p1-final下的新目录，拒绝覆盖和写入工程目录。工具不调用调试器、USBPcap实时接口、串口、磁盘写入或刷机工具。字段级ticket自动判定仍待最终schema和事件对齐，现为UNRESOLVED；脚本运行前后的人工许可不可由等待超时替代。

示例（仅离线准备，路径用实际最终AXF替换）：

```powershell
python Tests/UsbLifecycleFinal/hardware_tools.py prepare --axf <approved.axf> --mode ram --prefix ram_initial --out output/MSC24/udlm-p1-final/prep-ram-NEW
python Tests/UsbLifecycleFinal/hardware_tools.py snapshots --manifest <manifest.json> --folder <saved-hex-directory> --out output/MSC24/udlm-p1-final/decoded-NEW
python Tests/UsbLifecycleFinal/hardware_tools.py pcap --capture <saved.pcapng> --device-filter "usb.bus_id == 1 && usb.device_address == 2" --out output/MSC24/udlm-p1-final/pcap-NEW
```

VERIFIED（工具软件）：12项离线测试覆盖有效/异常HEX、magic/截断、地址错误、缺失快照、覆盖工程路径及命令前缀注入拒绝。它们不是12次硬件测试；原始报告为Tests/UsbLifecycleFinal/reports/offline_tools.json。

VERIFIED：准备工具还列出DBGMCU_IDCODE、GSNPSID的只读单字块，SD身份/claim和USB bridge对象仅按唯一ELF符号读取，不跟随Frame指针读取图像或SD数据。缺失/重名保持UNRESOLVED，不猜地址。12项工具检查包含空pcapng经实际tshark离线读取，空捕获不被判为协议成功。

## 最终固件离线准备

VERIFIED：p1-final05三个实际AXF与build_artifacts.json哈希一致，生成于output/MSC24/udlm-p1-final/prep-final05-{ram,sd_readonly,uvc}。preparation_final.json记录manifest/AXF哈希、宏和缺失符号：RAM/UVC各34块、4个模式专用符号缺失；SD只读38块、0缺失。缺失保持UNRESOLVED，未猜地址。这里只生成SAVE命令，没有执行；模式标签额外对照构建收据，manifest仍保留保守提示。NOT_TESTED：所有真实读取/抓包及协议结果，尚不具备硬件执行准入。
