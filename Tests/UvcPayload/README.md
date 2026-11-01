# UVC-09A：独立UVC Payload分包器

基线：`11f2ac93ee9dc08afa28b48b6b3cbfb395fc3997`。开发分支：`codex/uvc-09a-payload`。独立工作树：`C:\Users\29062\.codex\worktrees\uvc-09a\h7_mac`。完成检查后Commit/Push并提交审核，禁止直接修改或合并main。

## 模块及边界

新增CM7/Service/usb_uvc_payload.c/.h；仅依赖sys_result、C标准整数/布尔类型和memset，无HAL、USB Core、Camera、Media、帧率、分辨率或图像格式依赖。每个对象静态占有512字节包缓冲区，不使用动态内存，外部图像只借用、不修改。

Keil工程仅在CM7\Service组增加两个文件。CM4 Target XML与基线一致；BSP、Platform、SOF、现有Class、描述符、Probe/Commit、Camera/DCMI/DMA/Media全部未修改，也未接入EP1实际发送。

## 对外接口

| 接口 | 作用及重要返回值 |
|---|---|
| USB_UVC_PayloadInit | 初始化`{0}`对象，FID=0；已初始化返回ALREADY_EXISTS，不能借重复Init丢弃所有权 |
| USB_UVC_PayloadStartFrame | 借用外部图像、帧长、最大总包长；只允许空闲状态，包长3..512，零帧长拒绝 |
| USB_UVC_PayloadBuildPacket | 返回持久包指针、有效length、唯一Token；不推进偏移、不翻FID；待确认时重复Build返回同一包 |
| USB_UVC_PayloadCompletePacket | 调用方确认底层真实完成后，匹配Token才推进偏移；末包确认后结束帧并翻FID |
| USB_UVC_PayloadCancel | 取消未持有包的帧可立即结束；持有包时登记CANCELLING并返回BUSY，保留图像、包及Token |
| USB_UVC_PayloadAbortPacket | 底层已确认Abort完成，或明确从未接受发送时，匹配Token才结束取消、释放引用 |
| USB_UVC_PayloadReset | 仅IDLE允许复位新流FID；保留Token计数，活动/待确认/取消中返回BUSY |

同一对象API必须串行调用；模块不提供IRQ锁。未来应在主循环消费完成通知，或由外部临界区串行化。不同对象相互独立，Token只在所属对象内有效；禁止复制或搬移活动对象。

错误参数、状态、Token不匹配分别返回INVALID_ARG、INVALID_STATE/BUSY、NOT_FOUND；Token耗尽返回NO_RESOURCE。失败时不写输出视图，异常完成不推进偏移、不更改FID。取消返回BUSY表示已登记取消、等待归还，并非可以直接释放缓冲区。

## 包格式与长度

依据USB-IF UVC 1.1原始包中的`USB_Video_Payload_Uncompressed_1.1.pdf`，§2.4、Table 2-2（印刷页2–3，PDF9–10）：https://www.usb.org/sites/default/files/USB_Video_Class_1_1_090711.zip 。

```
Byte 0：02（Header长度）
Byte 1：80 | FID | EOF
Byte 2...：原始图像字节，顺序不变
```

FID=bit0，同帧保持不变；EOF=bit1，仅末包置1；EOH=bit7始终1。PTS/SCR、STI、ERR及保留位均0，不包含时间戳字段。规范描述FID在新帧边界切换；本实现正常情况下在上一帧末包确认后准备下一帧FID，线上可见行为一致。

最大总长度512，图像容量为512-2=510字节。因此38400=75×510+150：前75包512字节，末包2+150=152字节，共76包。恰好整除的帧在最后整包置EOF，不额外生成空Payload或ZLP。

包缓冲区4字节对齐，最后不足一Word的PIO安全尾部清零；返回length只含Header及图像，不含补齐字节。模块本身不执行PIO或DMA。

## 构包与异步完成分离

```
IDLE --StartFrame--> FRAME --Build--> PACKET
                     ^                |
                     |--非末包完成----|
                                      |--末包完成--> IDLE（FID翻转）
PACKET --Cancel--> CANCELLING --Abort确认/真实完成--> IDLE（取消结束）
```

- Build只建立待确认包：frame_offset与FID均不变；同一待确认包重复Build不复制图像、不增加Token。
- Token单调分配。重复完成在无待确认包时拒绝，旧Token不能命中下一包、下一帧或Reset后的包。
- Cancel待确认包时不改包内容、不释放引用、不翻FID；Build/Start/Reset保持拒绝。
- CANCELLING期间真实完成通知到达，只完成取消；SYS_OK表示确认被消费，不代表整帧成功。调用者需保留自己的取消意图，不能将其记为采集/发送成功。
- 未构造任何包就取消，不翻FID；已构造过包的取消在明确归还后翻一次FID，让下一帧与可能已到达主机的残帧分开，不伪造EOF。
- 新流Reset只能在底层和模块均已空闲时使用，不能每帧Reset。Reset不回退Token，UINT32_MAX耗尽后拒绝复用；必须在底层停止且旧软件通知排空后重新创建零值对象，不能活动中memset或重新Init。

## 调用示例（本阶段没有接入硬件）

```c
static UsbUvcPayload_t s_payload = {0};
UsbUvcPayloadPacket_t packet;

USB_UVC_PayloadInit(&s_payload);
USB_UVC_PayloadStartFrame(&s_payload, image, image_bytes, 512U);
USB_UVC_PayloadBuildPacket(&s_payload, &packet);

//此时偏移仍未推进，packet的数据必须保持到确认
//未来底层真实完成后，使用该次发送保存的Token调用：
USB_UVC_PayloadCompletePacket(&s_payload, packet.token);
```

上例省略调用方错误处理；实际使用必须检查每次返回值。若需要取消，先Cancel；得到BUSY后只有在确认底层不再访问该包时才能AbortPacket。模块不能替调用方判断硬件是否仍在访问，调用方不能伪造Complete/Abort确认。

## 参数与内存保护

拒绝NULL、零长度、Header不足以容纳图像的包长、超过512的包长、图像地址加法溢出、图像覆盖模块对象、输出视图覆盖模块或只读图像。当前帧未结束不能Start覆盖，Reset/Init不能丢弃待确认所有权。Complete核对待确认长度、剩余量及Token后才执行加法。

实际图像缓冲区是否已分配、可读以及生命周期仍是调用者责任；仅凭地址与长度不能验证任意指针对应的物理内存。本模块没有SDRAM地址、MPU/Cache或硬件存储区假设。

## 专项验收方法

ARMCC5分别-O0/-O2编译真实独立payload.c与测试，armlink无标准库链接，Unicorn执行ARM机器码。没有引入HAL/USB桩；测试仅提供基本C内存运行时函数。

62个独立场景，每个优化级别62项，共124次执行全部通过，两个版本均有：

- 38400B帧长度序列`[512]×75+[152]`，Header序列`[80]×75+[82]`，Token为1..76。
- 两连续帧152包；第二帧Header为`[81]×75+[83]`，FID在各帧内部稳定，仅确认边界变化。
- 精确逐字节比较独立图像模式，单帧模块读取恰好38400字节、两帧76800字节。
- 1、509、510、511、1020、1021字节帧；3、4、511、512字节最大包长和非对齐源地址。
- 重复Build、重复/错误完成、取消与完成交错、Abort确认、Reset、独立实例、Token耗尽。
- NULL、非法长度、地址溢出、内存别名及损坏偏移/待确认长度保护。

图像尾部后不映射内存；监视Build期间的图像访问，只允许位于有效图像范围的读取，不允许图像写入；模块对象前后设置哨兵；检查内部包的PIO尾部补齐。所有执行均无图像越界、无图像写入、哨兵完好。

独立Python oracle检查实际ARM输出的76包长度/Header/Token与源读取字节数，不仅检查C测试返回码。完整逐例记录见results.json；commands.json记录编译命令与编译器版本。

## 全量回归与正式编译

**最终结果：专项62场景×2优化级别，124/124通过；原461项全部通过。合计523个独立场景、585次实际执行，0失败。CM7/CM4完整重编均0 Error、0 Warning。**

原USB回归：EP1 Lifecycle45、Probe/Commit89、跨请求取消39、USB Core204、EP0 IRQ4、UVC Lifecycle40、Descriptor40，共461项全部保留。全部实际重跑，汇总见regression_results.json。

CM7、CM4分别使用Keil `UV4.exe -r MDK-ARM/h7_mac.uvprojx -t h7_mac_CM7/CM4 -o <log>`完整重编。CM7日志必须实际出现`compiling usb_uvc_payload.c`；CM4 Target没有加入该文件。默认业务未调用此模块，链接器可能裁剪未引用代码，不以固件尺寸变化作为是否编译的依据。

专项命令：`python Tests/UvcPayload/run_tests.py`。依赖本机ARMCC5、Unicorn、pyelftools，Python依赖通过既有python_deps路径导入，未加入仓库。

## Git及未验证内容

仅提交开发分支并创建审核PR，不直接更新main、不合并PR。未接入任何EP1发送、SOF、UVC Class或Camera/Media，也没有真实图像或实板USB验收。本阶段的软件分包正确性不代表异步硬件发送、主机显示或总线吞吐已经验证。
