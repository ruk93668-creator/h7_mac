# SDRAM三缓冲调整为每块40KiB

日期：2026-11-02；基线main a2c108f。仅调整预留容量和布局，不调整Application采集模式、图像格式、DMA传输时序或USB参数。

## 配置与容量

- CAMERA_STREAM_BUFFER_CAPACITY从38400B（37.5KiB）改为40×1024=40960B。
- 数量保持3；合计122880B（120KiB）。
- 首1KiB保留，总占用121KiB；当前BSP_SDRAM_WINDOW和MPU Region2已经是128KiB，因此无需扩大窗口。
- SDRAM基址0xD0000000、排他上界0xD0020000、64MiB物理容量均不变。
- MPU Normal、Non-cacheable、Shareable、Full Access、XN属性不变，仅更新覆盖范围说明并沿用一致性断言。
- Snapshot原有63KiB池保持不变，与Stream互斥复用；本轮修改的是三缓冲每块预留容量。

## 新地址布局（结束地址不包含）

|区域|开始|结束|容量|
|---|---|---|---:|
|保留区|0xD0000000|0xD0000400|1KiB|
|Buffer0|0xD0000400|0xD000A400|40KiB|
|Buffer1|0xD000A400|0xD0014400|40KiB|
|Buffer2|0xD0014400|0xD001E400|40KiB|
|未分配尾部|0xD001E400|0xD0020000|7KiB|

起始和40960B步长均为32B倍数，满足DMA 4B宽度及当前INCR4的16B对齐；三块互不重叠，编译期检查和运行时边界检查继续有效。Snapshot仍使用0xD0000400至0xD0010000，两种模式不可同时使用。

## 预留容量与实际帧长

当前CameraConfig仍为160×120 YUV422，实际帧长度仍是38400B，DMA仍配置9600 Word。每块尾部增加2560B预留，不能将整个40960B作为有效图像提交USB。Media仍根据配置计算frame_bytes、返回size_bytes；轮换只改变使用40960B的地址步长，函数算法没有改动。

UVC仍为160×120 YUY2，Probe/Commit帧长38400B，包长1023B；桥接仍校验38400B。增大预留容量不等于新增分辨率支持，也不代表DMA FIFO问题已解决。

## 修改清单

- CM7/Media/Src/camera_capture.c：容量常量改为40KiB；从第一个typedef至文件结尾逐字节核对不变，不改函数或状态机。
- CM7/BSP/bsp_sdram.h：更新128KiB窗口用途注释，明确1+3×40+7布局；地址宏不变。
- CM7/Core/Src/main.c：同步Region2覆盖说明，128KiB配置和静态断言不变；修改位于USER CODE区。
- Tests/CameraStreamState/stream_test.c：容量和地址步长预期改为40960，实际帧长预期保留19200/38400。
- Tests/SdramRange/run_tests.py：更新容量及排他上界，新增三个精确基址和7KiB尾部检查；保留原有负向测试。
- Tests/CameraBuffer40K/：本轮命令、结果快照、构建日志及保护校验。

## 检查与验收

|检查|结果|
|---|---|
|ARMCC编译真实Media后的布局/负向编译检查|22/22 PASS|
|三缓冲状态机，三种格式和独立随机模型|75/75 PASS|
|Media模式/生命周期及Snapshot回归|72/72 PASS|
|桥接所有权专项|35/35 PASS|

原有USB专项以512B为固定预期，当前main已由用户独立切换为1023B；本轮未重跑或更改这些旧协议断言，不声称USB全量回归通过。此次没有改USB正式源码。

构建脚本使用独立临时工程明确指定USB_MODE_BOOT_UVC=0/1，避免当前正式工程已开启UVC导致把UVC构建误标为TEST。分别完整重编CM7 TEST、CM7 UVC和CM4；最后用原工程恢复用户当前CM7构建配置。CM7 TEST、CM7 UVC和CM4完整重编均0 Error、0 Warning；当前配置恢复构建同样0 Error、0 Warning。具体尺寸见Tests/CameraBuffer40K/audit.json及日志。

复现：

```powershell
python Tests/SdramRange/run_tests.py
python Tests/CameraStreamState/run_tests.py
python Tests/CameraContextRefactor/run_tests.py
python Tests/UvcBridge/run_tests.py
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/CameraBuffer40K/build.ps1
```

历史Tests/SdramRange/verification_report.md记录的是原38400B步长，不改写历史数据；当前40KiB布局以本文及本轮测试为准。

未做实板新地址读写、连续采集或Windows视频验证，不以模拟结果代替硬件通过。用户uvoptx、正式uvprojx以及Application/USB/DCMI/SDRAM初始化源码原样保留；output/和tmp/保留。
