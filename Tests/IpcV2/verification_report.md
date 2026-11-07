# 阶段05B：IPC V2 实施与验收报告

## 结论

软件专项512/512 PASS，FAIL 0。正式CM7、CM4均完成ARMCC5完整重编，0 Error、0 Warning；随后补充整个4KiB共享窗口的输入/输出别名保护并统一注释空行，再执行512项专项及两核增量编译链接，仍为0 Error、0 Warning。最终源码的完整重编未重复执行，完整重编和最终增量日志分别保留，不能混淆。

实板双核冷启动、真实总线可见性尚未验证；未宣称Camera、USB或OLED实板回归通过。本轮没有运行这些业务的全套历史软件测试。

分支 `codex/stage05-ipc-core`，起点 `442cd4be953340ce5ce0a033fb053a13e367f047`。main不合并。保留原有video_config、Keil选项、旧MJPEG日志及所有未跟踪文件；用户原始文件备份在本地output目录，未提交。

## 修改清单与职责

| 文件 | 修改 |
|---|---|
| Common/Inc/ipc_status.h | 新增固定宽度本地状态、快照、诊断接口类型 |
| Common/Inc/ipc_shared.h | V2共享布局及大小、偏移、32B对齐编译断言 |
| Common/Inc/ipc_core.h | 纯C协议及本地Platform端口契约 |
| Common/Src/ipc_core.c | 单核写入权限、序号发布、有限读取、验证与本地诊断 |
| Common/Src/ipc_shared.c | 唯一320B、32B对齐.shared对象，无初始化值 |
| Common/Inc/ipc_config.h | 明确4KiB共享窗口；原心跳配置仅保留定义 |
| CM7及CM4/Platform/ipc_platform.c/.h | 编译器屏障+DMB、短PRIMASK临界区、线程检查 |
| CM7及CM4/Service/ipc_service.c/.h | 初始化职责分离及本地/对端快照接口 |
| CM7/Core/Src/main.c | EarlyInit改为检查返回值，失败进入原错误处理 |
| MDK-ARM/h7_mac.uvprojx | 两核Common组加入协议源文件和头文件，保留用户现有配置 |
| Tests/CameraFunctionLayout/Sdram/production_test.c | 同步EarlyInit测试桩返回类型，断言未修改；本轮未运行该历史套件 |
| Tests/IpcV2/ | 独立编译执行、故障注入、命令、结果和构建日志 |
| Docs/Design/IPC_V2.md | 协议及接口说明 |

不修改Scatter、MPU、HSEM0流程、CM4 main、RTOS任务、Camera、USB、MJPEG、DMA、OLED、HAL/LL。用户原有业务配置改变保留但不包含在本次提交中。

## 地址布局

共享窗口保持 `[0x38000000, 0x38001000)`，UNINIT。

| 内容 | 起始地址 | 大小 | 唯一写者 |
|---|---|---|---|
| Header | 0x38000000 | 64B | CM7 |
| CM7状态 | 0x38000040 | 128B | CM7 |
| CM4状态 | 0x380000C0 | 128B | CM4 |
| 保留窗口 | 0x38000140 | 3776B | 本轮不使用 |

总布局320B。两核正式Map均确认g_ipc在0x38000000、大小320B；RW_SHARED为UNINIT，Size=0x140，Max=0x1000。原Non-cacheable/Shareable MPU配置不变。

Header相对偏移：magic 0、version 4、layout_bytes 8、header_bytes 12、status_block_bytes 16、feature_flags 20、reserved[10] 24。数值分别为0x49504331、2、320、64、128、1、保留全零。

两核状态块内偏移完全相同：

| 字段 | 相对偏移 | 字节 |
|---|---|---|
| sequence | 0 | 4 |
| owner_id | 4 | 4 |
| block_bytes | 8 | 4 |
| ready | 12 | 4 |
| heartbeat | 16 | 4 |
| fault_flags | 20 | 4 |
| last_result（int32_t） | 24 | 4 |
| reserved[25] | 28 | 100 |

共享协议只包含uint32_t/int32_t及显式保留数组，没有指针、枚举对象或任务句柄。owner=7/4是协议核标识，不是HSEM编号。

## 启动与读写调用链

CM7原main等待CM4进入STOP → IPC_Service_EarlyInit → Bind → ColdStart：先撤销Magic，写Header和CM7初始块，最后发布有效Magic → 原HSEM0唤醒CM4 → Framework SERVICE Init：发布CM7 Ready=1 → 关闭初始化写窗口。

CM4原启动等待/唤醒 → Framework SERVICE Init → Bind → 验证完整Header → 仅初始化CM4块并提交Ready=1 → 关闭初始化写窗口。

CM7最终sequence=4；CM4最终sequence=2。初始化期间读取对端可能BUSY或读到旧UNINIT残留，不应用于启动屏障。本阶段不建立对端新会话身份证明。

普通调用方：IPC_Service_ReadLocal/ReadPeer → IPC_Core_ReadLocal/ReadPeer → 三次以内序号验证 → 成功才复制本地快照。读失败不改调用者输出。IPC_Service_GetDiagnostics取得本核本地累计故障和最近操作结果。

Core绑定和写接口仅供Service初始化。两核都在Scheduler之前各自main/Initcall串行写入；本阶段没有运行期写者，初始化关闭后Publish返回INVALID_STATE。后续周期发布需要专门设计唯一运行期所有者，不能直接让多个任务调用现有写接口。

## 一致性与错误边界

写：本地短临界区取得writer_busy → sequence写奇数 → 编译器/CPU屏障 → 独立32位volatile字段写入 → 屏障 → sequence写偶数 → 屏障 → 释放本地写权限。不在整个跨核发布期间关闭中断。

读：验证Header → 读取非零偶数sequence → 屏障 → 复制所有字段 → 屏障 → 再读sequence → 两次相等且偶数 → 再校验Header及字段 → 输出。最多三次，持续竞争返回BUSY。不用volatile假定多字段原子性。

sequence从0xFFFFFFFC可以发布到0xFFFFFFFE；下一次拒绝并返回NO_RESOURCE，不回绕复用，防止读者误认旧序号。无自动在线清零恢复。

未知Magic/Version/长度/功能位拒绝后续块访问，不向未知布局的错误字段写入。诊断只记录本核普通内存。仅CM7专用ColdStart有权在确定配套冷启动前提下重建Header；普通读写不重建未知协议。

拒绝NULL、未对齐、地址加法溢出、非法Ready/正数last_result；输入、输出和端口表都不能与整个4KiB窗口重叠，防止读接口变成跨核写入口。

## 测试与构建证据

实际命令：

```powershell
python Tests/IpcV2/run_tests.py
python Tests/IpcV2/run_build.py
python Tests/IpcV2/run_build.py --incremental
```

ARMCC5.06u7编译真实协议及真实Service，Cortex-M7/M4各O0/O2；128场景×4=512 PASS。独立Unicorn执行实际ARM指令。逐例结果见results.json，逐例说明见case_catalog.json，实际编译链接命令见commands.json。

覆盖布局、版本错误、所有权、成功/失败初始化、奇偶序号、有限重试、序号上限、每个字段的交错读写、写者重入、无效参数、共享区别名、PRIMASK恢复和初始化窗口关闭。屏障在模拟中为可计数桩，不能证明硬件内存排序。另用fromelf检查两核正式ELF，IPC_Platform_MemoryBarrier均实际包含DMB指令；这证明生成了指令，仍不代替双核实板可见性验证。

| 正式Target | 完整重编 | 最终保护修改后增量编译链接 |
|---|---|---|
| h7_mac_CM7 | 0 Error / 0 Warning | 0 Error / 0 Warning |
| h7_mac_CM4 | 0 Error / 0 Warning | 0 Error / 0 Warning |

构建记录包含编译前后源码指纹和工程SHA，确认构建过程中未改源文件/工程。完整和增量源码指纹不同是最终4KiB保护修改造成，非同版本重复结果。

## 实板验收步骤与待解决问题

1. 使用同一V2分支编译并烧录两核正式Target，保持现有双核装载方案，完整断电后重启。不能混烧V1/V2。
2. Watch查看g_ipc.header：magic=0x49504331，version=2，layout_bytes=320，header_bytes=64，status_block_bytes=128，feature_flags=1。
3. 两核初始化成功后查看m7.sequence=4、owner_id=7、ready=1；m4.sequence=2、owner_id=4、ready=1；所有保留字段0。Heartbeat当前始终0是预期，不是故障。
4. 在各核普通线程中使用Service读取接口，成功取得本地副本；不把裸Watch中的多字段刷新当作原子快照证明。不要在业务ISR中调用读取接口。
5. 继续验证原Camera/USB/OLED路径，记录实际结果；本轮尚未执行实板操作。

未实现周期Heartbeat、CM7业务状态发布、新RTOS任务、超时离线检测、热复位/单核复位恢复、真实boot_id。Ready只表示IPC Service初始化，不表示Camera/USB或后续Application全部成功。

一致快照可能是完整旧快照，SYS_OK不证明新鲜或对端在线。UNINIT保留区中的旧Ready/Heartbeat不具备会话证明；V2必须按配套固件冷启动验收。V1与V2不二进制兼容，旧V1代码本身不具备拒绝写新布局的保护。

本轮没有改公共时钟或原启动流程，单核复位风险仍然存在。写者卡死在奇数时只有限返回BUSY，不擅自恢复共享块。真正的跨核可见性、总线时序及高并发运行仍需后续双核实板验收。
