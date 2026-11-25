# 冻结源文件身份与本轮核对

日期：2026-11-25。以下SHA-256取自冻结Git对象，逐字节核对本地只读快照/main及当前QSPI文件。main提交 `62b42d7c0b7df9eadafa00953ee8ce1229da9826`；QSPI提交 `3a5c942dfd1420788102b11cc5271b188f319bf2`。未将用户本地Keil配置当作审核输入。

|输入|源码路径|SHA-256|
|---|---|---|
|main|`CM7/ApplicationV3/Core/video_config.h`|`ca1e5caadc210e0655e0c41b0c2c36aafb64526c4f731f85787a00199cdf1ebf`|
|main|`CM7/ApplicationV3/Core/video_config.c`|`54b8583ef2155ca530d1e6f6c5f6a003d36354790680ef7787408c85b459e0d7`|
|main|`CM7/ApplicationV3/Core/app_context.h`|`6c108e7cc3f06f2e8355c45cf29cbda1ec75ca1eb71de5f877056b81f4f308e1`|
|main|`CM7/ApplicationV3/Core/app_main.c`|`87f0c9908bbaa2d2a94124e3656116a0cf0dcf3cabc6f13ce48033d51322baa4`|
|main|`CM7/ApplicationV3/Core/app_tasks.c`|`c77c497c106bf26cb0763096ffacf31b4d2f6d53cdf21174a08e448a61aa2d5e`|
|main|`CM7/ApplicationV3/Services/Storage/storage_service.c`|`38984a2e937ca12b5c5dc5257a165fa42b9c19218332452bf16faaf657d69159`|
|main|`CM7/Service/mjpeg_avi.c`|`8fe795dd25c2451ceebfd57e74a9967e78f1eeb9e95585fff863daf7e7fab3a6`|
|main|`CM7/Service/mjpeg_avi.h`|`504555e93602bb2d3c4683f4211a5defcecc357d16ddbe7eb69f2a238c42f279`|
|main|`CM7/Platform/storage_fs.c`|`47570a0b655adddd94879ed85c154000f108d5504b6ca8d48f6bc8191d829423`|
|main|`CM7/Core/Src/main.c`|`f756ffe354befed6586d7c9e4d601f0b39ea5b4639a012227db2c07dbbc2dc84`|
|main|`CM7/Framework/framework_init.c`|`ae5990eff9f7ccbe1b70f9296e166f2fc5caede69eee0c7a6268d70b7ae9feed`|
|main|`CM7/Framework/framework_init.h`|`64c0c0b8f9f7a45850377f71f1022f392b6d57c485cea3f79a1bf0f11c80b227`|
|main|`CM7/Platform/platform_hal_timebase.c`|`375820a3a247fdaa73a7e8a61f5a8b3e580c897c0234caef22f7474c3da9edc6`|
|QSPI|`CM7/BSP/bsp_qspi.c`|`07b01f36b85ecb01ef90a6239f742032aad701939429d18556aefe6ad7cc12e8`|
|QSPI|`CM7/BSP/bsp_qspi.h`|`ff49f205443a12dc604cf04b9da1588d8e68231de48cc887a39da1da731aac76`|

本轮静态核对：15份源码与冻结对象一致；四份设计文档的本地相互引用存在；线格式区间总计4096B、数据体80B与独立Commit页无重叠；CRC标准向量0xCBF43926。只做设计算术和来源检查，不代表已实现加载器/提交器或通过掉电测试。新Mock、构建、实板全部NOT_RUN。

生产源码、正式工程、H1/H2归档均未修改。提交前检查仅新增Configuration设计目录；本地两份Keil修改和五份HEX保持未提交。完整方案入口：[Design](Design.md)。
