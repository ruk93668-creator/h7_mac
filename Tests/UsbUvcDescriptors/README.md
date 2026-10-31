# UVC-04：UVC 1.1描述符迁移验收

基线：`b49be0471cb5dcf663578d5c10a70faa40bc1fa6`。本报告记录该基线上的Descriptor迁移验收。用户随后确认上传，本报告随UVC-04成果提交；不实现UVC 1.1 Class协议。

## 规范依据

已取得[USB-IF官方UVC 1.1文档集](https://www.usb.org/sites/default/files/USB_Video_Class_1_1_090711.zip)，核对USB_Video_Class_1.1.pdf（Revision 1.1，June 1, 2005）：

- Table 3-3，印刷页50（PDF页63）：VC Header仍保留4字节dwClockFrequency，字段用途已废弃。
- Table 3-8，印刷页56至57（PDF页69至70）：PU长度10+n；iProcessing位于8+n，bmVideoStandards位于9+n。位图为0表示忽略该位图。

本轮不处理Probe/Commit，也不把VC Header保留的48MHz旧值当作后续协商时钟来源。

## 修改文件

|文件|修改原因|
|---|---|
|CM7/Service/usb_uvc_desc.c|更新版本、PU字段及级联长度；保留时钟字段/旧值；明确结构类型4字节对齐，实际存储184字节|
|CM7/Service/usb_uvc_desc.h|有效配置长度宏和接口注释更新为180|
|Tests/UsbUvcDescriptors/run_tests.py|保留原20项并更新预期；新增4项实际编译数据检查|
|Tests/UsbUvcDescriptors/README.md|本报告与新长度、偏移和PIO说明|
|Tests/UsbUvcDescriptors/results.json|本轮24项专项结果及两份描述符源码SHA|
|Tests/UsbUvcDescriptors/commands.json|本轮ARMCC5编译、链接命令及输出|
|Tests/UsbUvcDescriptors/regression_results.json|204+4+40回归及两核完整重编摘要|
|Tests/UsbUvcDescriptors/protected_sources.json|禁止范围与正式工作区保护、原20项编号保留审计|
|Tests/UsbUvcDescriptors/cm7_keil.log|CM7完整重编日志|
|Tests/UsbUvcDescriptors/cm4_keil.log|CM4完整重编日志|

## 修改前后

|项目|UVC 1.0基线|UVC 1.1本轮|
|---|---:|---:|
|bcdUVC|0x0100（00 01）|0x0110（10 01）|
|VC Header bLength|13|13|
|PU bLength|11|12|
|PU bControlSize|2|2|
|PU bmControls|00 00|00 00|
|PU bmVideoStandards|不存在|0，忽略位图|
|VC类专用wTotalLength|51（33 00）|52（34 00）|
|VS类专用wTotalLength|77（4D 00）|77（4D 00）|
|Configuration wTotalLength/返回长度|179（B3 00）|180（B4 00）|
|配置存储结构sizeof|180|184|
|返回地址对齐|4字节|4字节|

## PU字段偏移

PU起始于Configuration偏移57。bmControls保留原两个字节。

|字段|PU内偏移|配置内偏移|长度|字节|
|---|---:|---:|---:|---|
|bLength|0|57|1|0C|
|bDescriptorType|1|58|1|24|
|bDescriptorSubtype|2|59|1|05|
|bUnitID|3|60|1|02|
|bSourceID|4|61|1|01|
|wMaxMultiplier|5|62|2|00 00|
|bControlSize|7|64|1|02|
|bmControls|8|65|2|00 00|
|iProcessing|10|67|1|00|
|bmVideoStandards|11|68|1|00|

## 完整长度核算

- VC类专用：13 + 18 + 12 + 9 = **52**。
- VS类专用：14 + 27 + 30 + 6 = **77**。
- Configuration：9 + 8 + 9 + 52 + 9 + 77 + 9 + 7 = **180**。
- 完整14段bLength：9、8、9、13、18、12、9、9、14、27、30、6、9、7。

接口/Alt布局、1→2→3拓扑、YUY2 GUID、Format 1、Frame 1、160×120、10FPS、38400字节、1000000×100ns、EP1 IN、FS等时512字节与间隔1全部保留。不增加JPEG/MJPEG。

VC Header的时钟字段仍在相对偏移7，字节仍是00 6C DC 02；后面的接口集合字段仍在11/12，没有删除或移位。

## PIO和存储边界

```text
s_uvc_config_storage（类型和对象均4字节对齐，sizeof=184）
  descriptor[180]   偏移0..179，全部返回Core
  padding           偏移180，显式保留1字节
  类型尾部对齐空间   偏移181..183
```

180可被4整除，最后一个Word覆盖偏移176..179；若按EP0 64字节分包，最后一包52字节，也不需要读取偏移180。仍保留显式padding，并对结构类型施加4字节对齐；不能仅凭变量地址对齐推断结构sizeof。编译期断言与ELF对象符号st_size均确认184。

专项测试检查实际返回指针、ELF符号地址/大小、最后一个Word及尾部存储；有效返回长度始终180，不把padding返回主机。设备描述符仍返回18字节、存储20字节。

没有修改BSP、HAL或FIFO配置来规避边界问题。

## 实际验收

使用ARMCC 5.06 update7 build960编译真实描述符模块，在Unicorn执行导出函数，再逐字节解析返回数据；不是只检查注释或宏。

|测试|执行|PASS|FAIL|
|---|---:|---:|---:|
|迁移前原专项基线|20|20|0|
|迁移后专项：原20项+新增4项|24|24|0|
|USB Core|204|204|0|
|EP0 IRQ竞态|4|4|0|
|UVC Lifecycle及接口请求|40|40|0|

新增21：13字节VC Header、旧时钟字节与字段布局不变。
新增22：PU每个字段偏移及新增bmVideoStandards逐项正确。
新增23：ELF实际存储184、指针4字节对齐、有效长度180、尾部Word及保留空间安全。
新增24：将本轮编译所得配置移除新增字节、恢复四处版本/长度字节，SHA与迁移前编译的179字节配置一致，证明其他线格式参数未变化。

原20项编号、主体及安全检查全部保留，更新的是迁移后版本/长度/新增字段的正确预期。没有跳过或降低测试统计。

执行命令（仓库根目录，Unicorn/pyelftools需可导入）：

```powershell
python Tests/UsbUvcDescriptors/run_tests.py
python Tests/UsbCoreDecoupling/run_tests.py
python Tests/Ep0IrqRace/run_race.py
python Tests/UvcLifecycle/run_tests.py
```

|Keil Target|构建方式|Errors|Warnings|Code|RO-data|RW-data|ZI-data|
|---|---|---:|---:|---:|---:|---:|---:|
|h7_mac_CM7|完整重编-r|0|0|59518|2522|332|16340|
|h7_mac_CM4|完整重编-r|0|0|20334|914|636|2580|

## 范围及未验证项

除两份描述符源码及本专项目录外，2344个已跟踪文件与修改前SHA一致。Class、Platform/Core、Mode、BSP、官方HAL/LL和USB Core、Camera/DCMI/DMA/Media、CM4均未修改；Keil工程配置不变。

正式D:/32/STM32H7/h7_mac工作区未写入：其原有未提交Class结构体保持原样。验收完成时开发工作树HEAD/main仍为b49be04，Git暂存区为空；这是当次保护检查的历史状态，不是上传后的当前状态。

- 本轮没有Probe/Commit、GET_INFO、EP1 Open、SET_INTERFACE Alt1、Payload或视频发送。
- 仅描述符宣告升级，不代表整个Class已经实现UVC 1.1协议，也不把34字节Probe/Commit接口加入本轮代码。
- 默认TEST设备组织不变，未进行Windows UVC枚举、真实YUY2字节排列、USB视频、PTS/SCR或10FPS实板验收。
- 既有usbd_conf.h接口数量宏与Class协议实现留待后续独立任务，不在本轮修改。
- 验收阶段未提交、未推送、未合并；后续用户已明确确认上传，按该指令提交并推送这10个审核范围文件，Class未提交改动继续保留。
- protected_sources.json记录验收时的保护结果，不是可在任意后续提交直接套用的CI基线。
