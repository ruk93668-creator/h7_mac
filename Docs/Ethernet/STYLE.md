# Ethernet 代码规范审查

本轮实际读取本地 main f0ce45f 中的 bsp_camera.c/h、ov5640_device.h、platform_sync.h，以及已有 CameraStyleCalibration 记录，不只套 clang-format。远端 main 查询超时，参考版本明确固定为本地 main。

## 上次遗漏的问题

- 私有状态 ring/statistics/state 等没有主线 s_ 前缀。
- reset/configure/running/address/next 等内部函数没有模块前缀。
- 局部变量和回调参数大量用 r/p/a/b/c/n，接收容量、输出长度和帧长度不清晰。
- 小写裸类型与主线 Xxx_t 类型体系不一致。
- 多个结构体成员、变量声明挤在同一行，字段缺少准确说明。
- 公开头文件存在无参数名的原型，接口契约不便阅读。
- 内部实现、回调表和公开入口交错，没有主线的内部声明/公开实现/内部实现分区。
- 回调表是位置初始化；MDC 选频是多层连续三目表达式；超时及轮询上限没有命名。

## 本轮修正

私有状态统一 s_，内部函数按 EthMac_/EthDma_/EthPhy_/EthBoard_ 命名；内部代码使用 Eth..._t 类型。已有 eth_* 函数名称和错误码数值保留，旧类型名保留为 typedef 别名，兼容已有 PHY 和收发调用者。没有增加第二套状态机或管理框架。

结构体每个成员单独声明，注明状态、所有权或计数口径；接口参数完整命名，多参数签名和循环头按主线展开。输入容量 capacity、帧长度 frame_length、输出长度 length 分开。指针门禁显式比较 NULL。

模块组织为包含文件/常量/静态状态、内部声明、回调表、公开接口、内部实现。回调表用指定成员初始化，MDC 选频展开为明确分支，原频率范围和分频值不变，补 12 个真实初始化路径边界测试。保留 UTF-8 及主机 /utf-8 设置，不改写其他模块编码。

本轮有私有符号和类型整理、声明拆分、分支展开，不再声称非注释 Token 完全一致。此前 style_results.json 是旧提交的纯排版历史证据。本轮证据为 style_review_results.json。

## 验证和边界

原有七项主机回归通过；MDC 分频 12 个边界通过；五个底层模块 ARMClang C99 -Wall -Wextra -Werror 通过；旧类型/函数指针调用独立编译通过。descriptor 仍为 32 字节，池仍为 12544 字节，RX buffer 偏移仍为 6400，完整 CM7 smoke 链接地址仍为 0x24078000。

完整 Keil 构建 0 Error / 0 Warning，Code/RO/RW/ZI=67194/2846/1220/82636；相对上一测试固件 Code 增加 20 字节，其余不变。没有修改 MPU 数值、scatter、硬件 pin/clock 数值、DMA 所有权流程、公开函数名或测试命令。Application、Framework、Media、CM4、USB、Storage 和 FreeRTOS 不变。

本轮没有重做实板通信；前面的抓包和板端反馈属于整理前版本，不能当成本版已实板验收。测试固件需要重新下载后再验证。

## 仍需单独处理的职责问题

LAN8742A 协商模式的特定寄存器解析目前仍在 MAC 后端，后续可下沉 PHY 层。本轮先完成代码规范，不把协议职责迁移混入同一变更。可移植核心仍通过现有回调访问硬件，初始化仍由调用者显式发起。
