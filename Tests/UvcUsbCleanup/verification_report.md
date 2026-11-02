# 主线USB-only移植验收

按用户确认：保留main当前Application，不新增静态彩条应用，仅迁入UVC-10A的USB整理、接口契约和GET_CUR兼容修复。

- main基线：3367401d1cb97ec8f0f765639bd0545ecfbe098c。
- USB来源：0ecf94a5599d311e609c46671528bbebc9088a86。
- 独立验证分支：codex/uvc-10-main-migration。

## 最终范围

USB改动为7个Service文件：usb_mode.c/.h、usb_uvc.h、usb_uvc_class.c、usb_uvc_desc.c/.h、usb_uvc_payload.h。保留初始GET_CUR返回默认能力、不授权Commit、正常协商和Alt1约束。

保持main的app_main.c、app_camera.c及现有Application逐字节不变，不添加app_uvc_test.c/.h，不添加摄像头桥接；BSP、Platform、HAL/LL、ST Core、Media、CM7 Device、SDRAM、Scatter和Keil工程文件不变。另按用户追加要求同步M4：27个受版本管理文件与10A当前工作树逐字节一致，其中实际变化只有oled_device.c启用既有强制全亮调试命令。Payload.c算法不变。原OLED诊断文档保留。

main的USB冷启动选项命名为USB_MODE_BOOT_UVC，默认0保持TEST；置1只注册UVC Class，不改变Application、不生成彩条、不自动提交相机图像。09C的USB_MODE_UVC_TEST_ENABLED彩条开关不迁入main。即使选择UVC，没有外部SubmitFrame也不会产生视频；相机与UVC的实际集成仍由用户自行实现。

## 软件回归

|套件|执行数|PASS|FAIL|
|---|---:|---:|---:|
|USB Core|204|204|0|
|EP0 IRQ|4|4|0|
|UVC Lifecycle|40|40|0|
|描述符/字符串|40|40|0|
|Probe/Commit|89|89|0|
|EP0取消/GET_STATUS|39|39|0|
|EP1 Lifecycle|45|45|0|
|Payload 62项×O0/O2|124|124|0|
|Streaming|36|36|0|
|接口契约|14|14|0|
|合计|635|635|0|

原621次USB回归全部保留；另加10A的14项契约专项。只有Probe78/83按已实板验证的默认查询行为更新预期，继续验证默认数据及不授权Commit。09C的24项Application彩条测试不迁入，因为用户明确排除该Application；它们仍在09C/10A开发分支保留，没有为掩盖失败删除USB测试。

## 完整编译

ARMCC5 V5.06 update7 build960，Keil -r完整重编。CM7 UVC专用临时工程使用USB_MODE_BOOT_UVC=1，正式工程保持默认TEST配置。三个Target均0 Error、0 Warning：

- cm7_uvc_keil.log：USB Class选择UVC，不包含彩条应用。
- cm7_keil.log：默认TEST及原相机Application。
- cm4_keil.log：原CM4工程。

实际Code/RO/RW/ZI、日志路径与源码SHA见migration_audit.json。复现命令：

```powershell
python Tests/UvcUsbCleanup/run_regression.py
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/UvcUsbCleanup/build.ps1
```

## 安全及遗留项

先在全新工作树完成源码迁移、回归、完整构建和范围审计，再采用快进方式同步主线；不整包合并PR #3/#4。主线OLED原本额外的清屏/填充0xFF本地改动已先备份，再按10A同步；Keil配置未提交修改、原09C工作树以及output/、tmp/保留。

本轮仅软件验收，没有对移植后的二进制重新实板测试。默认应用保持main原状，不能把历史静态彩条显示成功视作当前主线已接通真实相机视频；OV5640与USB_UVC桥接尚未实现。

## M4追加同步验收

M4来源是用户指定的UVC-10A当前工作树，不仅是其已提交版本。27个CM4文件逐字节对齐；只有oled_device.c与main已提交基线不同，包含原有A5强制全亮测试，不新增其他OLED逻辑。主线旧OLED不同版本已备份到Codex本地backups目录，未丢弃。

同步后CM4再次完整重编0 Error、0 Warning。额外OLED异步刷新50/50 PASS，11156项断言，源码执行前后无变化。旧脚本原先与历史source_before.json比较，报29个历史差异；改为执行前后SHA核对，并将历史29项另列在source_integrity.json，历史文件和50项功能断言均未改。测试修复的是过时完整性基线，不放宽刷新状态、数据或错误断言。

USB635次结果不受CM4单文件诊断同步影响；没有把OLED模拟通过当成真实OLED显示通过。
