# Q1.5-A 独立软件共存专项

冻结 main `62b42d7c0b7df9eadafa00953ee8ce1229da9826`，QSPI输入 `3a5c942dfd1420788102b11cc5271b188f319bf2`。本目录不属于正式工程；正式Keil只新增CM7 BSP/HAL登记，不创建QSPI任务。构建脚本仅在输出树的测试项目中登记coexistence.c，保留真实V3五任务、Framework和两核启动，不更换主函数。

依赖：Python 3.11+、unicorn、pyelftools，ARMCC5/Keil位于脚本列明的D:/32工具链。环境中PYTHONPATH可指向已有本机依赖目录；不把运行库提交Git。Git对象需包含上述两个冻结提交（浅克隆应先取得它们）。所有命令在本分支工作树根目录运行，输出目录必须为新路径。

```powershell
python Tests/QspiFlashQ1/Integration/audit.py output/QspiFlashQ1/Integration/audit02
python Tests/QspiFlashQ1/Integration/run_mock.py output/QspiFlashQ1/Integration/mock02
python Tests/QspiFlashQ1/Integration/build.py output/QspiFlashQ1/Integration/build01
python Tests/QspiFlashQ1/Integration/artifacts.py output/QspiFlashQ1/Integration/build01
python Tests/QspiFlashQ1/Integration/regression.py output/QspiFlashQ1/Integration/bsp01 --group bsp
python Tests/QspiFlashQ1/Integration/regression.py output/QspiFlashQ1/Integration/v3_01 --group v3
python Tests/QspiFlashQ1/Integration/run_history.py output/QspiFlashQ1/Integration/build01 output/QspiFlashQ1/Integration/history04
```

构建脚本从冻结对象导出对照底座，候选覆盖严格四文件白名单；禁止把其他未提交修改当作输入。baseline/candidate各建CM7 JPEG、Raw、CM4，再建独立JPEG测试目标。只有临时工程选择视频模式；正式工程TargetOption和CM4 Target保持原样。每个构建检查0/0，并对比PT_LOAD实际字节，而不是要求带调试路径的AXF完全相同。artifacts.py离线生成/验证HEX并计算内部扇区范围，绝不下载。

生产BSP去掉历史SFDP/Quad/H1条件include与附加入口，产品固定120MHz、写策略空。audit.py用真实ARMCC预处理比较默认模式token，证明移除条件层未改产品行为。原BSP测试从QSPI冻结对象提取，51/516/48/45/11和真实HAL测试直接跑候选；只有历史30MHz及H1诊断需在临时树重建旧条件层。该覆盖不会应用到生产工作树，也不视为产品开放30MHz或写入。

V3产品Runtime、MediaLease等直接使用产品源码；历史Startup/Board/联合/USB Trace使用原BoardProfiles覆盖层，且只应用于另一临时树。run_history.py不改原FIFO断言，使用ARM标准assert，在失败入口停止仿真记录原FAIL；O2标准库只保留abort时不伪造源码行号。SDRAM/DCMI/USB历史FAIL/ERROR逐项与冻结main对照。

证据保存在evidence；最初错误调用（H2 PC漏参数、USB Trace误用无覆盖树、FIFO宿主映射/断言入口适配）均保留并标注补跑位置。collect.py用于本次固定目录收集，输入含历史错误和补跑目录，不能拿缺失路径的全新运行直接覆盖已提交证据。原始软件日志不会因补跑变成PASS。

独立测试线程协议：默认门闩0，等待AppV3_BusinessReady后再等待 `0x51313541`；消费一次后Init/ID/Status，随后两个阶段分别以20ms总期限读取FFE000的256B（0B和6B），阶段间让出10ms，完成/失败后每秒休眠不再访问Flash。没有Program、Erase、Recover、自动Init重试，也没有V3身份登记或消息。

软件Mock测试线程的门禁和阶段，并不模拟真实优先级调度；BSP故障由独立原专项覆盖，V3启动/背压由真实Runtime和FreeRTOS队列夹具覆盖。实际六任务调度下的CPU、心跳、服务时延和栈水位仍需实板验证。空闲共存不等于活跃采集/UVC/录像共存，全FF比较不等于Quad非均匀物理通路证明。

本轮不下载。生成镜像需要后续审核Option Bytes、SWAP_BANK、两核替换范围、当前板上H2及回退方案。不得直接打开原用户Keil点击下载这些新镜像。
