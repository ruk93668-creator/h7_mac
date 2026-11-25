# Q1.5-A ApplicationV3 空闲共存软件交付

状态：**SOFTWARE_REVIEW_READY / HARDWARE_NOT_RUN**，等待GPT独立审核。不是整个持久化系统SOFTWARE_COMPLETE，也不是READY_FOR_HARDWARE_ACCEPTANCE。本阶段完成后按授权的审核门停止，R2/Q2/Q3/Q4准备尚未实施。

冻结main：`62b42d7c0b7df9eadafa00953ee8ce1229da9826`；QSPI输入：`3a5c942dfd1420788102b11cc5271b188f319bf2`。工作分支 `codex/qspi-v3-coexistence`。原QSPI工作目录与用户Keil修改未覆盖，完整本地备份在原目录 `output/QspiFlashQ1/coexistence/local_preserved/`，备份含原文件、diff及SHA。

## 实际生产差异

只有四份生产/工程文件：新增 `CM7/BSP/bsp_qspi.c/.h`，启用 `CM7/Core/Inc/stm32h7xx_hal_conf.h` 的HAL QSPI，仅在 `MDK-ARM/h7_mac.uvprojx` 的CM7组登记BSP和现有ST HAL QSPI源文件。未增加生产Initcall或调用者，因此正式产品没有自动QSPI访问。

BSP移除历史H1/SFDP/Quad条件include和附加入口；默认120MHz、公共写策略 `{0,0}`、HAL私有句柄、原子权、错误/FAULT/Recover保持。真实ARMCC预处理后与QSPI冻结输入默认路径token完全相同，摘要见 [scope.json](../../Tests/QspiFlashQ1/Integration/evidence/audit02/scope.json)。历史测试条件层只在隔离树重建，不能进入生产源。

V3全部源文件、CM4、Framework、IPC、MPU、IRQ、FreeRTOS配置、ST HAL源码未改。两个TargetOption的XML语义完全一致，CM4完整Target一致；未改变产品消息52B、服务命令16B和IPC80B，独立测试以真实头文件编译断言。

## 独立测试目标

新增文件均在 `Tests/QspiFlashQ1/Integration/`。测试专用Initcall只创建静态768 words、优先级1的第六个线程，不登记V3 Runtime身份、队列或Supervisor，不修改正式五任务。保留原main/HSEM/Framework/BOOT及真实V3服务初始化。

等待BusinessReady及一次RAM门闩后，只执行Init、缓存ID、只读SR及256B的0B/6B。固定参考地址FFE000，期限每次20ms、阶段间10ms让出；成功/失败后静止，保留诊断。没有WREN、Program、Erase或Recover调用，没有自动重试。实板性能、TIM6实际推进、真实栈水位、同级Storage/IPC调度均NOT_RUN。Mock中600 words只是桩返回值，绝不能当作实板栈证据。

## 测试与原始结果

新线程专项：O0/O2各16项，合计 **32/32，770条断言**。包含未就绪/未授权零访问、启动超时与Tick回绕、任务创建失败、一次门闩、Init错误/BUSY、ID/QE/WEL异常、读超时/短完成/非均匀失配和完成后无再访问。其BSP为API桩；真实BSP故障由下表原专项另行覆盖，不将两类测试混称完整硬件调度仿真。

|BSP组|实际本轮结果|来源|
|---|---|---|
|Q1.1|51/51，989断言|evidence/bsp_results/mock|
|Q1.2|516/516，285723断言|evidence/bsp_results/q12/mock|
|Q1.3|48/48，25971断言|evidence/bsp_results/write_mock|
|Q1.4|45/45，12757断言|evidence/bsp_results/q14/mock|
|R1/R2|11/11，1349断言|evidence/bsp_results/q14_r2/mock|
|H2 Mock|8/8，2661断言|evidence/bsp_results/h2/mock|
|SFDP|6/6，600断言|隔离历史条件层|
|Quad ID|30/120MHz各11/11，各1466断言|隔离历史条件层|
|H1|2+4+13项通过，共13810断言|隔离历史条件层，不是实板写入|
|真实ST HAL|链接/5000ms Init超时、公共写门禁、恢复及H2真实HAL28断言通过|相应hal_link、q13/hal_gate、q14/hal、h2/hal|
|PC工具|H1备份工具10项、H2报告5项通过|原日志及h2_pc_retest.log|

Q1.1/Q1.2断言统计与旧报告不同，没有删断言或改旧用例。本轮在同一新main头文件环境下用**原QSPI冻结BSP**做独立对照，得到同样989/285723；原始结果见evidence/bsp_control。不能继续引用旧环境992/285725冒充本次运行统计。

V3 O0/O2均通过：产品Runtime/Service各62、产品Raw各8、MediaLease各35、Dispatcher各2、UVC Reader各11、StorageFS/FatFS/AVI各2；隔离历史Startup各73、Raw各8、联合USB/SD各6、Record各2、USB Trace各2。Camera Smoke/Repeat各5/2；USB Smoke/Repeat/Recovery各12/11/10；Storage各5/2/3。历史覆盖只应用于临时树；产品V3文件未残留测试版本。

原始失败保持：FIFO O0/O2均13 PASS/3 FAIL（5、7、8），前后每例相同；SDRAM18/26、DCMI88/133（5475断言）、USB旧组errors=9等与冻结main相同，未标成全套PASS。见 [历史比较](../../Tests/QspiFlashQ1/Integration/evidence/history04/comparison.json)。没有修改无关模块消除失败。

初次编排错误也保留：H2 PC测试漏必需参数；USB Trace被误放在无历史覆盖的树，出现未声明测试类型；FIFO宿主mmap与ARMCC O2断言入口适配未完成。已按原夹具要求修复运行器、补跑通过，未改原测试断言或生产源码。原始日志及纠正映射见 `evidence/invocation_corrections.json`，不能把这些原始失败日志覆盖成PASS。

## ARMCC5完整Rebuild和资源

|目标|ROM B|RAM B|最大静态栈 B|构建|
|---|---:|---:|---:|---|
|冻结main JPEG / 集成产品JPEG|145556 / 145556|102256 / 102256|1264 / 1264|各0 Error/0 Warning|
|冻结main Raw / 集成产品Raw|145556 / 145556|102256 / 102256|1264 / 1264|各0/0|
|冻结main CM4 / 集成CM4|28676 / 28676|4512 / 4512|432 / 432|各0/0|
|独立JPEG共存测试|152916|107032|1264|0/0|

共7次完整构建。JPEG、Raw、CM4各自的PT_LOAD地址及字节与同模式基线一致；AXF包含不同调试路径，SHA不同不代表下载内容不同。产品Map没有BSP_QSPI或coexist测试符号；被剔除的BSP不算运行证据。独立目标则实际链接Init/Read/Status、真实HAL和报告符号，写API不可达且未出现在链接符号集合中。

测试相对产品增量ROM7360B、RAM4776B，主要静态对象：BSP ZI668/RW17，测试ZI4080/RW8，另有少量对齐。测试栈3072B计入测试ZI，未改变产品栈或使用SDRAM。1264B为链接器全局静态分析，不能代替测试线程或main的真实最小水位，也不覆盖所有异步嵌套情形。

原始构建日志、资源、源SHA和镜像比较见 [build_report.json](../../Tests/QspiFlashQ1/Integration/evidence/build01/build_report.json)。ABI/资源和二进制对照是本阶段软件证据，不是Camera/UVC/SD真实时延保证。

## 离线镜像范围（无下载许可）

独立M7测试HEX数据 `[0x08000000,0x0802519C)`，若SWAP_BANK=0涉及Bank1 Sector0/1。M4数据 `[0x08100000,0x08106C88)`，涉及Bank2 Sector0。它们会替换现有两核固件，不得假定板上的H2或旧CM4仍同时运行。HEX逐条检查长度/校验和/地址/EOF，并与AXF加载字节独立比较一致。

M7 HEX SHA-256：`110fbcf0971ab974e020c6fddd6f134f1eb3de3aff67cd3451143866c4ce8ddd`。

M4 HEX SHA-256：`3030484c03d4983a192be39142dce7b6dfdb8e6c93a49e1f8285ee846540fb17`。

完整AXF/HEX/MAP哈希与符号见 [image_manifest.json](../../Tests/QspiFlashQ1/Integration/evidence/build01/image_manifest.json)。本机工件位于隔离工作树 `output/QspiFlashQ1/Integration/build01/`；未把二进制或运行库提交Git。离线审阅包 `coexistence_candidate01.zip` SHA-256：`2938e020528b2c61f8f0b3f3a1bcbe87cb44307ca1021bc10bd9709c747aac24`。

Option Bytes、SWAP_BANK、当前H2状态、供电、内部Flash备份/回退均需后续独立审核。保留H2当时取消内部备份的事实。H1/H2原始现场证据不在本轮工作树被改写；本轮没有连接实板、下载、故障注入或Flash修改。

## 阶段审核及后续

请求GPT独立审核四文件白名单、默认BSP等价证明、完整原始测试/失败、静态线程边界及七次构建。当前阶段仅为空闲共存软件准备；活跃业务控制入口、真实CPU竞争、时序/供电、读数据完整性和硬件栈全部NOT_RUN。

整个持久化交付继续按 R2→Q2-A→Q2-B→Q3→Q3-R→Q4准备推进，每阶段仍提交代码和报告并等待独立审核。当前不开放产品Slot，不修改V3启动，不处理配置Commit，也不宣称已达到最终SOFTWARE_COMPLETE。
