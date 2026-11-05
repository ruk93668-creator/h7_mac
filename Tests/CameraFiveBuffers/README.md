# 五缓冲实施与验收

## 范围

当前开发分支 codex/cm7-native-runtime-stage02；本次基线64a5252。生产修改仅四个文件：

| 文件 | 修改 |
|---|---|
| CM7/Media/Inc/camera_capture.h | 同步五缓冲接口注释，公开接口不变 |
| CM7/Media/Src/camera_capture.c | 五块600KiB描述符，分开ready_index/in_use_index，最新READY策略和完整所有权检查 |
| CM7/BSP/bsp_sdram.h | 业务窗口128KiB扩大到4MiB，物理容量仍64MiB |
| CM7/Core/Src/main.c | USER CODE区域的MPU Region2与编译期窗口检查同步为4MiB |

真实配置仍160x120 YUV422，38400B/帧，DMA 9600 Word；600KiB是单块预留容量，不是传输长度。640x480双字节帧仍超过现有65535 Word限制，验证接口继续拒绝，不宣称已支持此分辨率采集。
Snapshot仍使用原63KiB空间，与Stream互斥，共用起始地址；未扩大Snapshot功能。

## 状态与原子性

最多两块DMA、一块READY、一块IN_USE，其余FREE。IN_USE和READY可并存；应用仍只能借用一帧。FindReplaceable优先FREE，必要时READY；BSP替换失败不修改任何软件映射、索引、序号或丢帧统计。替换成功后释放旧READY、将新完成帧标记READY，IN_USE保持不变。
Get在原PRIMASK临界区内将READY转成IN_USE；已有IN_USE时拒绝第二次获取。Release只释放IN_USE，保留已经准备好的READY。Stop/DRAINING公开行为不变，原租借epoch/lease/ACK协议不变。
所有权校验遍历固定5个描述符，不扫描或复制像素。未修改BSP的CT/NDTR窗口检查、TC/FRAME配对、中断优先级或USB协议。

## 内存布局

| 项目 | 地址/大小 |
|---|---|
| SDRAM物理容量定义 | 64MiB，FMC Bank2 0xD0000000 |
| MPU业务窗口 | [0xD0000000,0xD0400000)，4MiB |
| 保留区 | 首1KiB |
| Buffer0 | 0xD0000400 |
| Buffer1 | 0xD0096400 |
| Buffer2 | 0xD012C400 |
| Buffer3 | 0xD01C2400 |
| Buffer4 | 0xD0258400 |
| 缓冲池结束，不包含 | 0xD02EE400 |
| 每块容量 | 614400B，600KiB |
| 五块总容量 | 3072000B |

全部32B对齐、无重叠，窗口尾部剩1121280B。MPU保留Normal、Non-cacheable、Shareable、可读写、禁止执行属性。源代码检索中没有发现其他正式SDRAM使用者，Scatter和链接段未改动；这些缓冲通过地址描述符管理，不是AXF中的3MiB ZI数组。

## 测试

ARMCC5 5.06 update7 build960；Unicorn执行真实ARM机器码。汇总见同目录results.json，逐例结果见对应专项的结果文件。

| 检查 | 结果 |
|---|---|
| python Tests/CameraStreamState/run_tests.py | 90/90；原75项保留并按新状态契约更新，新增15项 |
| python Tests/CameraContextRefactor/run_tests.py | 72/72 |
| python Tests/SdramRange/run_tests.py | 24/24 |
| python Tests/NativeRuntimeStage02/run_all.py | 103/103，含原97项及此前工作区6项诊断计时测试 |
| python Tests/UsbUvcDescriptors/run_tests.py | 40/40 |
| python Tests/UsbUvcDescriptors/run_string_tests.py | 8/8 |
| python Tests/Ep0IrqRace/run_race.py | 4/4 |
| python Tests/NativeRuntimeStage02/run_build.py | CM7 TEST、CM7 UVC、CM4完整重编，均0 Error / 0 Warning |

共341次测试执行PASS（包含重复运行的字符串检查）；USB专项原始输出保存在本机tmp/five_buffers_usb_results；生成的对象、AXF、临时工程及重复的USB原始日志不入库。当前正式CM7输出AXF为UVC配置。

原断言更新依据：三缓冲时IN_USE期间完成帧只能丢弃；现在第一张新帧成为READY，之后只淘汰旧READY。1000次完成的丢帧从1000变999，持有帧字节仍逐字节验证不变；30次持有期间完成从30变29。非法索引从3改为5，继续严格拒绝。独立3000步参考模型同时检查所有五块状态、索引、序号、丢帧和硬件目标。
新增覆盖READY与IN_USE并存、归还后立即Get、越界IN_USE、隐藏IN_USE、五块首尾独立写入、DMA长度上限、双所有权期间替换失败不提交。IRQ延后执行和PRIMASK恢复测试继续保留。

33ms相机源、1000ms模拟的Native集成用例：原三缓冲15帧USB EOF，新五缓冲25帧EOF；相机完成31帧、淘汰5帧、仍持有一帧。该用例原先要求少于25帧，已针对新设计改为精确25帧断言，未改变USB节奏实现。

## 边界与待验证

本次未重新执行全部历史USB/DCMI旧夹具；阶段02已有的历史不兼容/失败记录未被修复或宣称通过，专项通过不等于全仓所有历史测试通过。
此前桥接耗时统计、用户USB头文件注释、Keil选项与工程布局、output和tmp均保留，不作为本次五缓冲生产修改提交。工作区103项中的6项计时诊断来自此前未提交工作；本次提交的Native原97项均通过。

无实板操作：新增SDRAM高地址完整可读写、真实DMA/FMC总线竞争、MPU运行效果、USB25FPS、NVIC抢占和任务栈余量仍待实测。模拟写入不证明真实SDRAM高地址有效。

烧录后保持原采集配置，先确认init_error为0、采集和USB无错误；观察request_wait_ms是否下降，并重新用FFmpeg测接收FPS。再检查DMA地址全部落在上表范围且没有指向IN_USE。高地址读写可用独立调试测试执行，避免破坏正在采集或被USB借用的帧。
