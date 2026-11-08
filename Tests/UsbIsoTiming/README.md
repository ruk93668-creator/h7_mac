# USB EP1 ISO时序采样

本轮以旧C组为采集基线，SD测试代码与d030531保持一致；SD request保持0。只增加编译开关保护的观察点，不修改HAL/LL、ST Core、USB状态机、Payload算法、端点参数、优先级、Camera、Media、IPC、OLED、WFI或时钟设置。不排查M-V。

## 烧录与第一步

打开原 MDK-ARM/h7_mac.uvprojx，选择 h7_mac_CM7，确认 APP_USB_ISO_TIMING_TEST_ENABLED=1。旧C仍为 BLOCK=1、STRESS=1、WRITE=0，不含D/POLL_ONLY宏。若Keil仍打开修改前工程，关闭时不保存旧配置再重开，避免旧配置覆盖新增文件和宏。Rebuild、Download，完整断电重启。

先保持采样未触发，打开视频，确认是否仍约16FPS。若已变25FPS，先反馈，不得将正常时序当作旧C异常证据；采样代码和64B控制区可能改变布局，观察点本身也可能影响时序。

## Watch

展开全局 g_usb_iso_timing：

| 字段 | 含义与预期 |
| --- | --- |
| enabled | 1，DWT已可用；0且state=4则不可采样 |
| request | 手动置1开始；置2提前结束 |
| state | 0等待、1采样、2冻结待收尾、3可导出、4不可用 |
| count | 最多512条，不能据此换算长期FPS |
| writers | READY时必须0 |
| record_bytes | 48 |
| buffer_address | 0x24020000 |
| buffer_bytes | 24576，即24KiB |
| cpu_hz | DWT换算参考，TIM6墙钟不依赖此频率换算 |
| stop_reason | 1记录满，2手动停止 |
| magic / version | 0x5554494D / 2 |

确认16FPS后，只设置 g_usb_iso_timing.request=1。不要设置 g_sd_block_test.request，不要打断点或单步。通常不到一秒记录满并冻结，等state=3、writers=0后才导出。若一直state=1，可request=2，但count=0表示没有事件，不能分析。采样中再次request=1会拒绝，不重置正在写入的数据。

## 导出

READY后可暂停再导出。当前AXF的命令已由run_link_audit.py生成到dump_trace.ini。Keil Command执行两行：

```text
SAVE usb_iso_trace.hex 0x24020000,0x24025FFF
SAVE usb_iso_meta.hex 0x200026E8,0x20002727
```

文件通常位于MDK-ARM目录。两份必须来自同一轮READY，下一轮开始前先保留旧文件，避免覆盖。也可执行 INCLUDE D:/32/STM32H7/h7_mac/Tests/UsbIsoTiming/dump_trace.ini。元数据地址属于当前AXF，后续重新修改代码后须重新运行链接审计生成；记录区由Scatter固定。

项目根目录解析：

```text
python Tests/UsbIsoTiming/decode_trace.py MDK-ARM/usb_iso_trace.hex --meta MDK-ARM/usb_iso_meta.hex --output output/UsbIsoTiming/capture
```

生成CSV逐条记录和JSON统计，也可直接把两份HEX交给我分析。解析器校验HEX校验和、元数据版本、READY、完整记录及有效墙钟；不会制造采样数据。

## 事件与分析

| 编号 | 观察位置 | 意义 |
| --- | --- | --- |
| 1 DATAIN | UVC当前事务匹配成功的DataIn回调 | 软件收到完成通知，不是硬件XFRC置位的精确瞬间 |
| 2 CONFIRM | UsbTask开始处理WAIT_CONFIRM | 任务开始消费该完成事件 |
| 3 SUBMIT | Class调用USB_Device_Transmit之前 | 下一包提交，sequence为事务凭据 |
| 4 HAL_ENTER | BSP完成PIO暂存复制后、调用HAL之前 | 开始界定HAL操作范围 |
| 5 HAL_EXIT | HAL返回后 | 目标帧/FIFO准备在4与5之间，不能声称记录了内部精确写入时刻 |
| 6 RESULT | 上层提交返回非成功 | BUSY或错误，不能算成功发送 |
| 7 FRAME | 末包确认后 | 软件整帧完成点，不伪造硬件FRAME事件 |

时间对照：DATAIN到CONFIRM观察唤醒/确认延迟；CONFIRM到下一SUBMIT观察分包、复制及等待；SUBMIT到HAL_ENTER观察Core/BSP与PIO暂存复制；HAL_ENTER到HAL_EXIT界定HAL和PIO窗口；HAL_EXIT到DATAIN观察提交后等待；FRAME边界单独区分帧间等待。

每条保留硬件DSTS帧号、DIEPCTL、DIEPTSIZ、TX FIFO可用Word、SysTick相位与IPSR。寄存器是先后读取，不是同一原子硬件快照；目标帧命令位读回值未必能单独还原写入意图。时间范围包括观察点开销，不能据几微秒差异直接宣布根因。

## 计时与一致性

DWT由SCCB等模块共用，本采样仅使能、不清零。DWT在WFI期间可能停计，因此解析器不把跨睡眠DWT差值当作墙钟等待。等待时间使用既有1MHz TIM6与uwTick的有界一致性快照；最多尝试两次，处理UIF待处理边界，检查原ARR/CEN/UIE/Sleep时钟。无效时记录time_flags，不输出伪造耗时。未独立标定实板微秒频率。

控制结构64B置于DTCM，让Watch request不被AXI Cache副本遮挡。512×48B记录位于独立AXI 0x24020000～0x24025FFF，Scatter窗口保留32KiB，避开0x24000000的32KiB IDMA和0x24010000的压力缓冲；不新增MPU区。记录区只由CPU写，ISR不打印、不扫描整个缓冲；短PRIMASK区预留唯一槽位，最后提交event标记。冻结后必须所有写者结束，UsbTask才Clean该独占区域的DCache并发布READY。收尾Clean也可能造成一次性扰动，不算无扰动性能验证。

采样后state=3禁止再写，解析仅使用count条，数组不会循环覆盖。request=1可开始新轮，但应先导出上一轮。未触发时快速返回，不在每次任务唤醒中为采样进入临界区。

## 软件验证与限制

- ARMCC5.06 update7 build960：真实采样器42/42执行测试通过，涵盖O0/O2、关闭时无资源、IRQ上下文、PRIMASK恢复、容量、重入请求、冻结、Cache任务收尾、DWT回绕、TIM6无效与UIF边界。
- Python解析8/8通过；实际AXF布局与边界11/11通过。
- 旧C SD回归556/556通过；保留的USB/UVC、Camera、IPC、OLED回归1899/1899通过（采样宏未开启的历史测试路径），未修改其断言。
- 当前CM7采样开启构建0 Error、0 Warning；CM4增量检查0 Error、0 Warning。采样关闭的ARM模块编译与历史回归通过，未另外生成整机关闭镜像。
- 实板采样及故障复现未验证。编译和模拟通过不能证明USB故障已定位或修复。

验证入口：run_tests.py、test_decode.py、run_link_audit.py。构建记录build_results.json、CM7_build.log、CM4_build.log，链接信息link_audit.json。dump_trace.ini仅适用于其中记录的AXF。


## 第二轮：通知与任务路径细分

本轮不改变通知、任务优先级、SOF规则和SD轮询。新增记录点：

|事件|位置|用途|
|---|---|---|
|8 NOTIFY_BEGIN|EP1 DataIn完成Core回调后，调用通知前|完成处理到通知的耗时|
|9 NOTIFY_END|通知函数返回后，仍在IRQ内|通知调用耗时；不是IRQ退出点|
|10 WAIT_RETURN|UsbTask的App_EventsWait刚返回|任务已获得执行机会|
|11 PROCESS_ENTER|UsbTask_Process入口|等待返回到处理入口|
|12 UVC_ENTER|正常路径首次USB_UVC_Process调用前|命令及Mode处理耗时|
|13 SOF|官方Core SOF回调后、任务通知前|对照真实SOF相位|
|14 WAIT_BEGIN|UsbTask调用App_EventsWait之前|区分待处理通知与重新等待|

48B记录格式、512条上限和静态空间不变，仍使用版本2。新增事件后窗口更短，不要求包含两张完整帧。解析器将每个DATAIN到同序号CONFIRM之间的事件配对，缺失阶段不补造，notification_packets保留逐包结果，notification_metrics汇总各段。SOF与DataIn仍共用原通知，不把一次等待返回伪称为独立消费一个通知。

notify_end_to_wait_return包含剩余ISR、其他异常、调度及等待函数返回前的开销，不能直接当作纯调度耗时。process_to_uvc包含原有任务处理及采样自身开销。当前只采样，不修复发送策略。

烧录当前CM7镜像，先确认未触发时仍约16FPS；如果变成25FPS，先反馈，不能把它当成异常路径证据。SD request保持0，将g_usb_iso_timing.request设为1，等state=3、count=512、writers=0，再暂停导出。为保留第一轮文件，本轮使用新名字：

```text
SAVE usb_iso_trace_detail.hex 0x24020000,0x24025FFF
SAVE usb_iso_meta_detail.hex 0x200026E8,0x20002727
```

以上地址已经由本轮实际AXF复核；后续重新链接须再核对。解析命令：

```text
python Tests/UsbIsoTiming/decode_trace.py MDK-ARM/usb_iso_trace_detail.hex --meta MDK-ARM/usb_iso_meta_detail.hex --output output/UsbIsoTiming/detail16
```

第二轮软件结果见refined_results.json：采样器42/42、解析器10/10、布局12/12、USB Core201/201、EP0竞态4/4、Native USB11/11。CM7和CM4现有Target构建0 Error、0 Warning（增量构建，非全量重编）。新实板记录尚未取得，不宣称根因已确认。
