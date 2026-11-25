# Q1.5-A/B 实施准入建议（Phase 0-R1）

本轮冻结 main 为 `62b42d7c0b7df9eadafa00953ee8ce1229da9826`，QSPI 输入为 `5bdd963862431b490f7e12b21f94b8e7f19ac9e9`。本计划替代旧 Application 底座计划。只完成文档和差异审核，Q1.5-A 尚未获执行准入。底层资源和真实V3入口见 [兼容性矩阵](Q1_5_Phase0_Compatibility.md)。

## 文件白名单

|文件或范围|未来A阶段建议动作|
|---|---|
|CM7/BSP/bsp_qspi.c/.h|选择性移植审核输入的BSP及私有操作权、诊断和空公共写策略；产品测试条件/Tests依赖迁出方法先提交明确差异，不借集成重构已验收API|
|CM7/Core/Inc/stm32h7xx_hal_conf.h|只启用HAL QSPI模块|
|隔离分支的MDK-ARM/h7_mac.uvprojx|以新main工程增补CM7 HAL QSPI和BSP文件登记；保留CM4排除项、V3登记、Supervisor keep、Scatter、输出和其他编译参数|
|Tests/QspiFlashQ1/Integration/（拟新增）|独立测试目标、静态线程、一次门闩、独占内部RAM缓冲、时序/首错/资源报告，Mock与构建/Map/HEX审计|
|Docs/QSPI/|记录冻结输入、白名单、回归、资源、结果及后续单独下载方案|

生产侧不增加Service、Mailbox、Queue、Record、Supervisor组件或RuntimeTarget。ApplicationV3全部36文件、原Control/IPC任务、Platform、Framework、CM4、FreeRTOS、HSEM/IPC ABI、MPU、IRQ与Scatter均不列入修改白名单。ST HAL源码沿用冻结main。若产品消费者或业务控制入口确需改变，先独立申请。旧QSPI工程登记的m7_project_test及H1/H2任务不移入产品工程。

独立软件共存构建在新隔离分支完成，名称建议 codex/qspi-v3-coexistence。本轮没有创建该分支。当前工作树Keil配置有用户修改，须保存原文件、diff和SHA，自动构建只操作隔离树。不能把用户调试宏、下载脚本或输出路径直接覆盖进冻结主线工程。

## A：V3隔离软件共存

1. 从冻结新main导出干净底座，单独重建主线JPEG、Raw、CM4作为对照。再按白名单增补产品BSP，记录源文件/工程/工件SHA和完整差异，0 Error/0 Warning。正式目标无测试入口；未被产品调用的BSP可能被剔除，另用HAL链接及共存测试目标证明实际可达。
2. 测试专用目标保留五个生产任务及原BOOT流程，在Tests目录独立翻译单元中通过测试专用Initcall仅静态创建一个低优先级线程。它不登记App_Events、不修改RUNTIME_TARGET_COUNT、不计入g_app_tasks_debug.created_tasks，不申请V3队列或替换业务回调。静态TCB/768 words栈和优先级1为待验证参数。
3. 测试线程先周期让出CPU，以只读AppV3_BusinessReady确认全部服务启动成功；该函数只能证明初始化就绪。默认门闩零，未获授权不触碰QSPI；启动失败或超时保存报告并停住。原Control的1s启动限时不能为了QSPI延长或绕过。
4. 门闩及就绪都满足后，线程独占QSPI Init、ID、状态和短读。建议256B/次，总预算20ms、调用间至少vTaskDelay(10ms)。仅9F/05/35/15/0B/6B，120MHz配置读回；03按现有门禁拒绝，Program/Erase公共写策略保持为空。无自动Init/Recover重试。
5. 读取发生在测试线程中，不修改服务栈、不在Camera/USB/Storage/Control的循环、消息回调、Supervisor或Frame Dispatcher中插入测试逻辑，不持Frame Lease、FatFS租约、队列锁或临界区等待HAL。禁止测试线程调用Set/Query/Recover/Read等Control专用Supervisor接口。
6. Mock验证五任务启动、结果背压/漏通知回查、就绪前拒绝QSPI、服务初始化失败及部分创建失败时测试停住、低优先级持续让出、同级Storage/IPC仍推进、QSPI BUSY/FAULT/清理失败保留资源。测试只能在独立夹具模拟IRQ/硬件，禁止生产新增Hook或直接篡改BSP锁存。
7. 集成前后执行下表回归，逐例保存断言数、原始FAIL与日志。单独核对52/16/80B ABI、Raw七缓冲、JPEG物理块和消费者身份、USB Reader Proof、SD存储独占及首错保留。分支实现以新main现有行为为准，不恢复旧Application路径。
8. A完成后停止，报告源差异、Map符号、RAM/ROM/静态栈和真实任务水位来源，重新计算候选HEX地址、扇区、SHA及回退对象；等待软件审查与后续下载授权。

初次Init和显式Recover最长预算5.2s，只在流未激活的独立维护阶段评估。QSPI在内部256B段间没有主动任务让出，整次大读仍占CPU；如需测试大块必须独立参数记录，不给每段重置完整期限。测试总时限和帧率阈值均为建议，尚不能宣称满足实板实时性。

## 回归矩阵与A通过标准

|组别|未来A要求|当前R1状态|
|---|---|---|
|QSPI历史|Q1.1 51、Q1.2 516、Q1.3 48、Q1.4 45、R1/R2 11、SFDP6、Quad ID两频率各11、H1固件19、PC10、H2 Mock8/真实HAL28断言/PC5；保留完整旧用例及各自频率|NOT_RUN，本轮文档审核|
|V3核心|产品Runtime/Service62、历史Startup73，O0/O2，真实Startup/BOOT身份、三服务发布和Control超时不被QSPI改变|NOT_RUN；计数为新main历史报告目标|
|V3媒体|MediaLease35、Dispatcher2、UVC Reader11、产品/历史Raw各8，O0/O2；保持读者退出和一消费者一Lease|NOT_RUN|
|V3存储和联合|USB/SD联合6、RecordBoard2、StorageFS+FatFs+AVI2、AVI上限2、USB平台诊断2，O0/O2|NOT_RUN|
|V3服务覆盖|Camera Smoke/Repeat 5/2；USB Smoke/Repeat/Recovery12/11/10；Storage5/2/3，各O0/O2；历史覆盖层只应用于临时测试树|NOT_RUN，不能应用到正式源码|
|历史FIFO|保留原16项中3项FAIL，旧/新main与集成候选按相同输入对照；不得删断言或更名跳过|NOT_RUN，原始历史证据保留|
|其他主线依赖|相机I2C1/电源、SDRAM/DCMI、SDMMC、FatFS/USB、IPC及Runtime专项，使用新main逐例对照|NOT_RUN，不拿旧f0测试结论替代|
|工程/资源|CM7 JPEG/Raw、CM4、独立共存测试完整Rebuild0/0；生产Map无测试对象，CM4有效源集及ABI不变，无新DMA/IRQ/MPU/栈优先级改动|NOT_RUN；本轮静态工程审查完成|
|同配置等价|若BSP未被调用，产品机器代码预期可保持，必须实际比对；新增未剔除只读数据和工程keep变化逐项解释|NOT_RUN，不提前承诺HEX相同|

V3历史计数来源为冻结主线 [regression_report.md](https://github.com/ruk93668-creator/h7_mac/blob/62b42d7c0b7df9eadafa00953ee8ce1229da9826/Docs/Architecture/V3/PreMerge/regression_report.md)。A回归必须重新执行，不引用历史PASS代替新候选结果。原有SDRAM/DCMI/USB错误若仍出现，保留原始值并确认是否新回归。

## B与生产控制路径的准入边界

冻结V3正式BOOT后Camera/UVC/Storage不会自动运行。仅新增独立QSPI测试线程而观察三个业务空闲，不能宣布活跃业务共存PASS。当前没有在本轮批准范围内新增Control命令输入或IPC控制协议。B在实板启流前必须明确已获准的真实产品控制入口。不得通过给测试线程伪造Control身份、修改TCB、直接调用Service或在Control循环插入Watch测试邮箱解决。已有历史独立软件夹具可保留原样运行，但不代表产品控制通路已接入；若B需要新的控制通路，单独提交范围与协议批准。

B计划沿用Camera→Camera+USB→Camera+SD→联合IPC的逐级只读对照，并同时记录帧率长尾、丢帧/Reader退出、SD写入/关闭/回读、Mailbox背压、事件循环等待、Control首错、心跳间隔和任务栈水位。建议同配置先60秒观察、10分钟联合采样、30分钟稳定性；帧率下降不超过5%、新增硬件/协议错误零是待审阈值。低优先级QSPI读取最多影响同级轮转并不等于没有影响，实际结果取得前均NOT_VERIFIED。

数据只读FFE000–FFFFFF，与既有参考按字节/CRC比对。全FF仅说明内容一致；不制作写图案。当前板卡最后下载仍为H2是既有用户确认。本轮不读取板卡、不恢复相机固件，后续下载须另定两核镜像、当前Option Bytes、实际扇区和回退。取消过原MCU备份的事实继续保留，不能声称恢复旧现场逐字节一致。

## 待审核决定

- 接受新main为唯一A底座，按白名单移植BSP；测试构建分支迁出生产的方法需在A给出逐项语义证明。
- 接受BSP直接接入，不增加产品Service/消息队列/Supervisor组件。
- 审核独立任务的就绪门禁、栈/优先级、256B/20ms/10ms参数和停止条件。
- 对活跃业务共存先明确Control合法控制入口及授权范围；当前列为B前置阻塞，不扩大本轮协议设计或实施。
- 审核帧率、丢帧、USB/SD长尾、Heartbeat与栈水位的对照标准。
- 保留H1/H2历史证据、下载准入缺口和所有延期实板NOT_RUN。

本轮最终状态为 PHASE0_R1_REVIEW_READY。等待GPT审核，不自动进入Q1.5-A。
