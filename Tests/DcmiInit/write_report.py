from pathlib import Path
import json,shutil
p=Path('Tests/DcmiInit')
for source,dest in [('Tests/SdramV0/Camera/results.json','camera_results.json'),('Tests/SdramV0/Camera/results.md','camera_results.md'),('Tests/SdramV0/Camera/commands.json','camera_commands.json'),('Tests/Ov5640Migration/cm4_results.json','oled_results.json'),('Tests/Ov5640Migration/cm4_results.md','oled_results.md'),('Tests/Ov5640Migration/cm4_commands.json','oled_commands.json')]:shutil.copyfile(source,p/dest)
data=json.loads((p/'results.json').read_text(encoding='utf-8'))
lines=['# CM7 DCMI+DMA 初始化模拟验收','','编译真实BSP、Framework和HAL GPIO/DMA/DMAEx/DCMI驱动；寄存器使用Unicorn内存模型，NVIC为调用记录桩。','', '| 测试输入 | 预期 | 实际 | 结果 |','|---|---|---|---|']
for i,row in enumerate(data['cases']):
 state=row['state']
 if i in [3,4,5,7,10]:expected='BUSY，NVIC不开放，不启动采集'
 elif i in [8,11]:expected='TIMEOUT，NVIC不开放，不启动采集'
 elif i in [6,9,12]:expected='IO，按阶段清理DMA，不开放NVIC'
 elif i in [13,14]:expected='首次失败后可重试成功'
 elif i==15:expected='真实HAL IRQ分别回调正确的私有句柄，Parent关联正确'
 elif i==16:expected='其他实例不进行GPIO/时钟初始化'
 else:expected='初始化与寄存器配置正确，无采集/传输启动，无SDRAM访问'
 actual=f"返回{state['result']}，DMA/DCMI初始化{state['dma_init']}/{state['dcmi_init']}次，NVIC掩码{state['nvic_enabled']}，DMA/DCMI CR=0x{state['dma_cr']:X}/0x{state['dcmi_cr']:X}，自动启动={row['access']['started']}，SDRAM访问={row['access']['sdram']}，断言{state['checks']}"
 lines.append(f"| {row['name']} | {expected} | {actual} | {row['result']} |")
lines+=['',str(data['summary']),'','Busy用例中的EN/CAPTURE位由测试预先设置，用于模拟其他活动占用；BSP拒绝重新初始化。IRQ用例仅注入状态标志，不启动DMA。']
(p/'results.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
report='''# CM7 V0 DCMI + DMA 底层初始化交付

## 结果

- ARMCC 5.06 update 7（build 960），CM7和CM4完整Rebuild均为0 Error、0 Warning。
- DCMI初始化模拟：20/20 PASS，1183项断言。
- SDRAM当前正式初始化回归：20/20 PASS，531项断言。
- OV5640回归：42/42 PASS，464项断言。
- CM4 OLED回归：12/12 PASS，3493项断言。
- 未下载或进行实板采集。所有初始化用例确认不启动采集、不启用DMA Stream、不访问SDRAM数据区。

## 修改点

| 文件 | 修改 |
|---|---|
| CM7/BSP/bsp_dcmi.c | V0引脚、DCMI HAL、DMA1 Stream0、双向句柄关联、NVIC、两个IRQ及BUS Initcall；失败返回通用结果码 |
| CM7/BSP/bsp_dcmi.h | 仅公开BSP_DCMI_EarlyInit和BSP_DCMI_Init，不公开HAL句柄 |
| CM7/BSP/bsp_dcmi_hal_config.h | 独立启用CM7 HAL DCMI模块，避免修改CubeMX生成的HAL配置 |
| CM7/Core/Src/main.c | USER Includes加入头文件；USER Boot_IPC在原BSP_Camera_EarlyInit前增加BSP_DCMI_EarlyInit调用 |
| MDK-ARM/h7_mac.uvprojx | CM7增加HAL DCMI源文件、--preinclude；现有CM7 BSP分组加入配置头，CM4仍排除该分组 |

参考旧D:/codex_proe/h7_codex/h7_mac/CM7/Core/Src/main.c的Camera_DcmiInit，保留来源副本v0_reference_main.c。没有迁移旧Camera_StartFrame、帧缓冲、统计变量、采集循环和帧完成回调。

## V0引脚

| DCMI信号 | 引脚 |
|---|---|
| D0 / D1 | PC6 / PC7 |
| D2 / D3 | PG10 / PH12 |
| D4 / D5 | PE4 / PB6 |
| D6 / D7 | PE5 / PE6 |
| HSYNC / VSYNC | PA4 / PB7 |
| PCLK | PA6 |

全部AF13、推挽、无上下拉、Very High。GPIOA/B/C/E/G/H时钟使能。不会重配PA8 XCLK、PB8/PB9 SCCB、PH15复位、PB10/PB11 I2C2和SDRAM引脚。

GPIOB配置寄存器由两核使用。为避免CM7配置PB6/PB7与CM4配置PB10/PB11发生HAL读改写冲突，在既有HSEM唤醒CM4之前完成预配置，之后BUS初始化不会重复修改引脚。仅新增用户区预配置调用，原等待、IPC EarlyInit、HSEM释放和双核时钟顺序均不变。

## DCMI参数

硬件同步、8位DVP、PCLK上升沿采样、HSYNC高极性、VSYNC高极性、所有帧、JPEG关闭、全部字节及全部行。HS/VS极性沿用旧V0测试配置，真实图像有效窗口需在后续采集阶段验证。

HAL_DCMI_Init按库行为配置LINE/VSYNC/ERR/OVR中断源，但不设置DCMI ENABLE或CAPTURE。初始化成功后清除DCMI标志和NVIC挂起，再开启NVIC；不调用任何采集启动接口。

## DMA参数

DMA1_Stream0，DMAMUX Request=DMA_REQUEST_DCMI（当前头文件数值75），外设到内存、外设地址固定、内存递增，外设及内存均为32位，Normal模式，High硬件优先级，FIFO关闭，Single burst。

不填写目标帧缓冲地址和NDTR传输长度，不开启Stream。32位DMA搬运与8位DVP不冲突：DCMI将8位像素流打包到32位数据寄存器；本轮不读取该寄存器或启动传输。

__HAL_LINKDMA建立s_dcmi.DMA_Handle=&s_dcmi_dma及s_dcmi_dma.Parent=&s_dcmi。两个句柄均私有。

DMA1_Stream0固定保留给DCMI；当前工程没有其他业务使用它。若首次初始化时Stream EN或DCMI ENABLE/CAPTURE已置位，返回BUSY，避免重置正在运行的资源。其他DMA1 Stream不复位、不关闭DMA1总时钟。

## NVIC及IRQ

DMA1_Stream0_IRQn和DCMI_IRQn均抢占优先级5、子优先级0，沿用旧V0 Camera_DcmiInit；当前CM7 SysTick为15，没有其他CM7外设IRQ使用该优先级。CM4 I2C2优先级属于CM4独立NVIC，不与CM7构成同一抢占关系。

```text
DMA1_Stream0_IRQHandler → HAL_DMA_IRQHandler(&s_dcmi_dma)
DCMI_IRQHandler        → HAL_DCMI_IRQHandler(&s_dcmi)
```

启动文件保留弱定义，正式C函数覆盖它们，没有重复强定义。最终AXF向量检查：DMA1 Stream0 IRQ编号11、DCMI IRQ编号78，向量均指向BSP的GLOBAL函数，见link_audit.json。

## 初始化路径和错误处理

```text
SystemClock_Config
    → BSP_DCMI_EarlyInit（唤醒CM4之前）
    → 原摄像头/IPC EarlyInit和HSEM唤醒
    → 原MPU Region2配置
    → Framework BUS：BSP_DCMI_Init
         → DMA1/DCMI时钟
         → HAL_DMA_Init
         → 句柄关联
         → HAL_DCMI_Init / MSP
         → 清挂起、设置优先级、开启NVIC
```

BUS阶段只初始化硬件，不依赖传感器已输出或SDRAM已可用，也不读写SDRAM；因此不依赖同一等级其他BSP先后顺序。后续Device、Service、Application照常运行，OV5640仍由原Application配置Y8彩条。

重复Init直接成功，不重新配置运行中的资源。HAL_ERROR映射IO，HAL_BUSY映射BUSY，HAL_TIMEOUT映射TIMEOUT；DCMI HAL初始化失败时尝试HAL_DMA_DeInit，NVIC保持关闭；清理失败返回IO。错误通过既有Framework Fail-fast返回main::init_error。

没有新增FrameCallback、FrameBuffer对象、采集API、设备状态机、调度框架或测试统计变量到正式固件。

## 模拟验收方法

实际编译执行当前bsp_dcmi.c、framework_init.c及未修改的HAL GPIO、DMA、DMAEx、DCMI源文件；HAL DMA/DCMI初始化外层仅用于错误注入，正常路径调用真实HAL。NVIC接口为记录桩，寄存器映射到Unicorn内存。

核对AF13和非目标引脚寄存器位不变、DMAMUX Request、DMA/同步寄存器位、句柄关联、HAL Ready、优先级和清挂起顺序。向真实HAL IRQ注入TC/LINE标志，确认回调收到正确私有句柄。监控寄存器写入，确保没有设置Stream EN或DCMI ENABLE/CAPTURE；监控SDRAM地址，访问次数为0。

同时覆盖资源忙、DMA和DCMI的ERROR/BUSY/TIMEOUT、DCMI失败后的DMA清理失败、失败重试、重复初始化及错误实例MSP。

模拟不验证真实PCLK波形、DVP走线、setup/hold、跨核电气行为、总线带宽或真实图像数据，不能作为已完成采集验收的结论。

## 命令和证据

```powershell
python Tests/DcmiInit/run_tests.py
python Tests/DcmiInit/Sdram/run_production.py
python Tests/SdramV0/Camera/run_tests.py
python Tests/Ov5640Migration/run_cm4_regression.py
python Tests/DcmiInit/audit.py
D:\\32\\UV4\\UV4.exe -r D:\\32\\STM32H7\\h7_mac\\MDK-ARM\\h7_mac.uvprojx -t h7_mac_CM7 -o D:\\32\\STM32H7\\h7_mac\\Tests\\DcmiInit\\cm7_keil.log
D:\\32\\UV4\\UV4.exe -r D:\\32\\STM32H7\\h7_mac\\MDK-ARM\\h7_mac.uvprojx -t h7_mac_CM4 -o D:\\32\\STM32H7\\h7_mac\\Tests\\DcmiInit\\cm4_keil.log
```

完整编译/链接命令和版本见commands.json。逐项输入、预期、实际见results.md/json。回归结果位于Sdram/production_results.json、camera_results.json、oled_results.json。

SDRAM隔离回归仅在新测试目录增加未执行启动段所需的BSP_DCMI_EarlyInit链接桩，不修改旧正式SDRAM代码。实际GPIO预配置由DCMI用例执行真实HAL测试。

audit.json确认：仅bsp_dcmi.c/.h、main用户区、uvprojx及新配置头变化；SDRAM、MPU、Cache、Scatter、OV5640、IPC、CM4和Framework均未改。最终AXF没有HAL_DCMI_Start_DMA、HAL_DMA_Start/Start_IT或此前已清理的SDRAM诊断变量。

CM7目前可在Framework_InitRuntime返回后观察init_error，以及私有s_dcmi.State、s_dcmi_dma.State、DCMI->CR、DMA1_Stream0->CR；初始化成功两句柄均READY，CAPTURE/ENABLE/EN均为0。没有执行实板下载。

用户代码位于独立BSP及main USER CODE；CM7通过新增--preinclude启用DCMI。CubeMX重新生成Keil工程时需保留自定义Group和编译选项。
'''
(p/'验收报告.md').write_text(report,encoding='utf-8')
print('report written')
