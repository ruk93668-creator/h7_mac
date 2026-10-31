# UVC-05：UVC 1.1功能名称字符串验收

基线：`036f667eafb1ca048e277e12b8d62db4d0b8b069`。仅补齐字符串描述符、索引关联及回调绑定；不开发Probe/Commit或视频发送。下方保留UVC-04历史记录，当前状态以本节为准。

## 修改清单

|文件|修改|
|---|---|
|CM7/Service/usb_uvc_desc.c|静态LangID/功能名称、两个取描述符回调、IAD及VC字符串索引5|
|CM7/Service/usb_uvc_class.c|仅在s_uvc_descriptors绑定LangID及Interface两个回调，其他协议逻辑不变|
|CM7/Service/usb_uvc_desc.h|必要的跨文件回调声明、有效长度常量；Class编译需使用这些声明|
|Tests/UsbUvcDescriptors/run_tests.py|原24项保留，新增8项字符串原始数据检查，并统一运行8项Core集成检查|
|Tests/UsbUvcDescriptors/string_control_test.c|真实UVC注册、GET_DESCRIPTOR分发、PIO及非法索引测试|
|Tests/UsbUvcDescriptors/run_string_tests.py|ARMCC5/Unicorn执行集成测试，监测字符串源区域的实际读取|
|Tests/UsbUvcDescriptors/.gitignore|忽略已汇入总报告的临时string_commands/string_results文件|
|本目录README/results/commands/regression_results/protected_sources及两核日志|更新本轮验收记录|

## 字符串数据

String Descriptor Zero：有效4字节，存储4字节，4字节对齐。

```text
04 03 09 04
```

Function Name：UTF-16LE的UVC Camera，共10个字符，有效22字节；静态存储24字节，4字节对齐。

```text
16 03 55 00 56 00 43 00 20 00 43 00 61 00 6D 00 65 00 72 00 61 00
```

有效协议数据后另有两个零字节，仅用于PIO尾部空间；不把结束符或padding发送给主机。两个getter均不分配内存，空length返回NULL，不支持的速度返回NULL并将长度置0。

## 索引关联和Core路径

- IAD的iFunction位于配置偏移16，值5。
- VC Interface 0的iInterface位于配置偏移25，值5；与IAD完全一致。
- VS Interface 1的Alt0/Alt1 iInterface保持0，Terminal/Unit字符串索引保持0。
- iManufacturer/iProduct/iSerialNumber以及iConfiguration仍为0。
- 两个用户字符串扩展宏仍为0，没有修改Core索引机制。

```text
GET_DESCRIPTOR(String)
  → OTG_FS_IRQHandler / HAL PCD
  → BSP Setup回调 → Platform Usb_Setup
  → USBD_LL_SetupStage → USBD_StdDevReq → USBD_GetDescriptor
      Index 0 → GetLangIDStrDescriptor → USB_UVC_GetLangIDStrDescriptor
      Index 5 → GetInterfaceStrDescriptor → USB_UVC_GetInterfaceStrDescriptor
  → USBD_CtlSendData → USBD_LL_Transmit → BSP PIO → HAL
```

USBD_IDX_INTERFACE_STR为0x05。索引1/2/3/4因对应回调为空而STALL；索引6及255走Core默认分支并STALL。测试直接调用正式USB_UVC_Register，不再为该专项测试替换描述符表；注册成功。

## PIO安全

- 功能名称源数组24字节，返回有效长度22。假设32位读取，最后一个Word为偏移20..23，落在已分配存储内。
- LangID有效长度/存储长度均4，恰好一个Word。
- BSP实际将源数据复制到已有PIO包槽，最后两个补齐字节为0；本轮不修改BSP或HAL。
- 用ELF符号检查实际存储大小与对齐，用ARM内存读取钩子监测字符串源区域；请求22/255字节均只交付22字节，短请求8字节仅交付8字节。
- 返回静态指针，交错/重复请求后地址和内容保持不变。

## 结果

|验收|结果|
|---|---|
|原描述符专项|24/24 PASS|
|新增字符串原始数据检查|8/8 PASS|
|新增真实Core字符串集成|8/8 PASS|
|本目录统一命令合计|40/40 PASS|
|USB Core回归|204/204 PASS|
|EP0 IRQ回归|4/4 PASS|
|UVC Lifecycle及接口请求|40/40 PASS|
|CM7完整重编-r|0 Error、0 Warning|
|CM4完整重编-r|0 Error、0 Warning|

原测试03的IAD期望按本轮要求更新为索引5；原测试24增加索引5显式断言，再归一化这两个新增索引，继续逐字节核对旧基线SHA。没有删除旧测试、跳过执行或取消安全断言。新增测试32另外证明配置有效长度、视频能力及所有其他字节与UVC-04完全一致。

运行：

```powershell
python Tests/UsbUvcDescriptors/run_tests.py
python Tests/UsbCoreDecoupling/run_tests.py
python Tests/Ep0IrqRace/run_race.py
python Tests/UvcLifecycle/run_tests.py
```

要求Windows ARMCC5、Unicorn、pyelftools。results.json包含32项原始数据检查和8项Core集成结果，commands.json包含两部分实际编译命令。run_string_tests.py也可单独运行；其独立中间JSON不提交，避免重复存档。

## 依据、边界与未验证项

依据USB-IF UVC 1.1原文第3.6、3.7.1、3.11节（[官方文档集](https://www.usb.org/sites/default/files/USB_Video_Class_1_1_090711.zip)），以及当前usbd_def.h、usbd_ctlreq.c的实际分发。

禁止范围源码与基线SHA一致；Class在描述符回调表之外的内容（包括Init/DeInit/Setup和26字节占位数组）逐字节不变。正式工程原有未提交的控制结构体保留，不纳入本轮提交。保护JSON记录验收时状态，不是任意后续提交的CI基线。

HAL收发使用桩，实际执行Platform、ST Core和BSP拷贝逻辑；源读取监测及ELF边界检查不能替代真实USB硬件验收。默认Mode仍选择TEST，没有切换到UVC。尚未实板验证Windows UVC枚举及USB总线行为。

Core当前按字符串索引选回调，不向这两个回调传入wIndex语言ID；本轮仅声明0x0409并验证该语言请求，没有新增语言过滤或修改Core行为。无已知失败用例。

本轮没有Probe/Commit、GET_INFO、EP1 Open、Alt1、Payload或视频发送。

---

## 历史：UVC-04描述符迁移记录

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
