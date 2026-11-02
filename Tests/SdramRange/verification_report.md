# SDRAM与MPU范围一致性整改

2026-11-02；工作分支main。仅整改内存范围，不改变Application采集模式，不接入相机USB桥接。

## 根因及硬件依据

最早SDRAM接入只开放首64KiB，诊断只读写首1KiB。随后三缓冲开发将MPU Region2改为128KiB、Media上界改为0xD0020000，却遗漏bsp_sdram.h的64KiB宏和main注释。BSP_SDRAM_WINDOW没有参与FMC初始化、不是芯片容量限制；正式固件无依赖此宏的64KiB硬件限幅，现存独立诊断只用它检查1KiB测试范围。

用户提供原理图SVG_Schematic1_2026-10-25.zip的SDRAM页标注IS42S16320F-7BLI。ISSI原厂数据手册标明512Mbit、32M×16，即64MiB。正式FMC参数13行、10列、4Bank、16位，计算8192×1024×4×2=67108864B，与原理图型号一致。FMC Bank2采用默认0xD0000000映射，正式源码未启用FMC地址重映射；芯片地址至0xD3FFFFFF。证据路径、SVG SHA、原厂手册URL见hardware_evidence.json。

- [ISSI数据手册](https://www.issi.com/WW/pdf/42-45R-S_86400F-16320F.pdf)
- [ST RM0399](https://www.st.com/resource/en/reference_manual/rm0399-stm32h745755-and-stm32h747757-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)

本轮不扩大MPU。Region2原本就是128KiB，仍为Normal、Non-cacheable、Shareable、Full Access、XN。Region0默认禁止外部区被Region2覆盖属于既有设计，IPC Region1地址独立。GPIO、PLL2、110MHz、FMC时序及初始化命令完全不改。

## 宏职责

|宏|最终值/职责|
|---|---|
|BSP_SDRAM_BASE|0xD0000000，FMC Bank2基址|
|BSP_SDRAM_CAPACITY|0x04000000，板载芯片物理容量；不代表MPU全部开放|
|BSP_SDRAM_WINDOW|0x00020000，当前业务/MPU可访问窗口128KiB|
|BSP_SDRAM_END|BASE+WINDOW，排他上界0xD0020000|
|CAMERA_STREAM_SDRAM_END|引用BSP_SDRAM_END，不再单独硬编码|
|CAMERA_BUFFER_OFFSET / BASE|保留首1KiB，BASE由BSP基址+0x400派生|
|CAMERA_BUFFER_POOL_SIZE|保留63KiB，Snapshot局部容量；不扩为整个窗口|
|CAMERA_STREAM_BUFFER_CAPACITY / COUNT|每块38400B，共3块，算法及容量不变|
|SDRAM_CHECK_BYTES|独立历史诊断的1KiB实际测试量；仍不在正式固件自动运行|

头文件检查窗口非零、2的幂、基址对齐及不超过芯片容量。main使用编译期类型断言核对窗口与实际MPU 128KiB编码。Media编译检查起始/步长32B对齐及Snapshot、Stream池均不超窗口。HAL的MPU大小宏包含类型转换，因此使用C常量表达式断言而不是预处理器#if。

## 最终地址布局（上界不包含）

|用途|起始|结束|字节数|
|---|---|---|---:|
|保留区/历史独立诊断|0xD0000000|0xD0000400|1024|
|Stream Buffer0|0xD0000400|0xD0009A00|38400|
|Stream Buffer1|0xD0009A00|0xD0013000|38400|
|Stream Buffer2|0xD0013000|0xD001C600|38400|
|窗口尾部未分配|0xD001C600|0xD0020000|14848|

所有缓冲区起始及步长均为32B倍数，也满足DMA 4B对齐；流缓冲互不重叠。Snapshot可用池为0xD0000400至0xD0010000，与Stream有意复用，并非两个并发分配；既有Media mode/state排他机制保证不同时使用。全局搜索CM7/CM4/Common/Board及Scatter未发现其他正式SDRAM分配者。历史诊断只触碰首1KiB，未加入正式固件。

## 修改文件

- CM7/BSP/bsp_sdram.h：64KiB遗留窗口更正为128KiB；区分物理容量与业务窗口，提供派生上界和静态检查。
- CM7/Core/Src/main.c：修正64KiB旧注释，增加MPU窗口静态一致性断言；Region2寄存器配置不变。
- CM7/Media/Src/camera_capture.c：地址引用BSP宏，补充编译期边界检查；不修改任何函数、状态机、DMA操作或轮换算法。
- Tests/SdramCleanup/production_test.c：当前正式代码MPU预期更新为128KiB；补齐后来新增的3个无关启动/USB隔离桩。原20项断言不删除、不放宽。
- Tests/SdramRange/：新增实际编译布局数据、负向编译测试、回归快照、构建脚本及本报告。

历史before副本、生成旧工程的脚本和早期验收报告保留原64KiB记载，它们不是当前配置来源。全局引用搜索结果见references_before.txt；本报告说明现行范围，不改写历史结果。

## 实际验收

|测试|PASS|FAIL|
|---|---:|---:|
|实际ARMCC目标文件布局与负向编译|18|0|
|正式SDRAM初始化/MPU/时钟/错误路径|20|0|
|Media上下文及模式/生命周期|72|0|
|三缓冲状态机/独立随机模型|75|0|
|USB完整回归|635|0|

范围测试读取ARMCC编译实际Media源码后目标文件内的常量，不只匹配注释。负向测试使用独立临时头文件，实际编译真实Media，确认64KiB不足、零窗口、非2幂及超过芯片容量被拒绝，正式文件不改。SDRAM测试执行真实初始化和MPU函数，HAL为Mock；Media测试执行真实ARM机器码，覆盖三格式与所有权。没有删除、跳过或放宽旧USB/Camera断言。

CM7 TEST、CM7 UVC、CM4均Keil -r完整重编0 Error、0 Warning，ARMCC5 5.06u7 build960。UVC临时工程仅加USB_MODE_BOOT_UVC=1，完成后移除；最终保留默认TEST构建。日志及尺寸见audit.json和本目录三个keil.log。

复现命令：

```powershell
python Tests/SdramRange/run_tests.py
python Tests/SdramCleanup/run_production.py
python Tests/CameraContextRefactor/run_tests.py
python Tests/CameraStreamState/run_tests.py
python Tests/UvcUsbCleanup/run_regression.py
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/SdramRange/build.ps1
```

## 本地保护与硬件风险

用户原有app_main.c连续采集选择、uvprojx和uvoptx逐字节保留且不提交，output/和tmp/保留。不改应用采集模式、Camera配置、USB协议、DCMI/DMA或BSP SDRAM初始化。生产文件只有上述3个。

本轮未连接实板，未执行0xD0010000以上真实读写、整片容量测试、地址线别名测试、110MHz温压及保持验证。原理图型号与FMC几何一致不等于证明当前焊接芯片/全部地址线正常；Unicorn不模拟真实MPU权限或SDRAM电气。建议后续在停采并确认无消费者占用后，独立备份/恢复验证三缓冲高地址，禁止在活动DMA/帧上做破坏性测试。本轮没有向正式固件加入自动测试。
