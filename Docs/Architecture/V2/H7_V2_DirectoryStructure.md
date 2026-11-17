# Application V2 目录设计与最终清理目标

状态：DESIGN_PROPOSED。输入基线 `8a03596`；本轮没有创建以下生产目录或修改 Keil。

## 1. 推荐目录

```text
CM7/ApplicationV2/                 # 最终唯一应用层；开发期与旧目录隔离
  Core/                           # 值类型、命令完成契约、启动/运行端口
    media_format.h                # R2条件澄清：中立媒体值/码域，非Camera配置类型
    v2_types.h
    v2_runtime_port.h
    v2_bootstrap.c/.h
    Runtime/                      # FreeRTOS 适配与五个既有角色的任务入口
  MediaGraph/                     # 静态图、只读查询、有界验证
    media_graph.c/.h
    media_graph_format.c/.h        # 仅Graph聚合/适配；基础媒体值归Core
  Lifecycle/                      # 每模块转换契约；资格与受控事务工具
    managed_lifecycle.c/.h
    camera_qualification.c/.h
    Transaction/                  # 保留已审定 Tx/Rx/Replay 行为；迁移需独立授权
  Resource/                       # 准入、Grant/Lease 关联、交接、只读核验
    media_resource.c/.h
    media_admission.c/.h
    media_transfer.c/.h
    Ports/                        # 既有 Media/Device/Platform 公开接口适配
  Modules/
    Camera/                       # Camera 生命周期与唯一停止推进
    USB/                          # USB 业务；UVC Service 保持独立
    Storage/                      # 文件与录像业务；独立封尾/错误结果
    Network/                      # 仅未来位置；本轮及初期不创建/登记实现
  Supervisor/                     # 配置生成计划与有界异步计划执行
  Configuration/                  # 产品模式、格式、静态拓扑、依赖计划
  Diagnostics/                    # 状态投影、IPC 应用载荷适配、只读 dump
```

目录是 V2 应用域的内部划分，不将所有 Graph、资源或协议能力强塞进 Framework。媒体后端依旧在 `CM7/Media`，驱动在 Device/BSP/Platform，USB/AVI 等 Service 不复制。Graph Core 保持纯 C，与 Application 的产品图表分离，后续需要复用时可以独立搬移；当前不为通用性再建 Framework 注册体系。

## 2. 职责、允许依赖和禁止依赖

| 目录 | 唯一职责 | 允许依赖 | 禁止依赖 |
|---|---|---|---|
| Core 值类型 | ID、结果、不可变视图 | 标准 C / Common 值类型 | 具体 Modules / HAL |
| Core/Runtime | 静态任务、BOOT、运输与上下文校验 | FreeRTOS、RuntimeEvent、模块公开接口 | 模块私有状态、寄存器 |
| MediaGraph | const 拓扑与格式合法性 | Core 值类型 | Resource、Lifecycle、Frame Release、HAL |
| Lifecycle | 命令转换、资格决策、受控事务 | Core、注入 Ops | BSP、端点、FatFS、Resource 私有账本 |
| Resource | 单所有者准入及身份关联 | Core、后端公开端口、资格只读/关闭能力 | 产品 Pipeline 策略、Supervisor、复制后端计数 |
| Modules | 各自业务状态及服务推进 | Core、Lifecycle、Resource 公开接口、各自 Service | 其他模块内部函数、HAL、BSP |
| Supervisor | 计划依赖、步骤身份和完成 | Graph、Configuration、模块消息接口 | Frame 地址、Device_Stop、FatFS、业务状态副本 |
| Configuration | 产品策略和 const 表 | Core/Graph 描述类型 | 正在运行的模块状态 |
| Diagnostics | 只读、带来源/时间的投影 | 模块/Resource 只读接口、IPC 适配 | 以 Debug 反向驱动业务 |

## 3. Runtime 和 Ports 为何不再增加顶层“万能层”

Runtime 负责消息收发、等待及调用模块；Ports 是明确的依赖注入边界。Ports 不另开硬件状态机；调用 Media、Device 或 Platform 的公开 API 并保留错误即可。若端口必须使用 HAL 细节，应留在既有 Platform/BSP，通过纯 C 函数表注入，不能放进 ApplicationV2。

CameraStream 是 `CM7/Media/Src/camera_capture.c` 内部实现，不假设存在同名独立文件。Graph 实体也不要求一实体一任务、一实体一 C 文件。R2第二轮推荐一个CameraCapture Entity内部封装DCMI/DMA；Raw/JPEG Provider为既有后端能力视图，不新增Pool。FrameRoute为纯逻辑拓扑，不新增运行时分发对象或账本。

## 4. Keil 切换与旧目录退休

现工程 `MDK-ARM/h7_mac.uvprojx` 含 `h7_mac_CM7`、`h7_mac_CM4`。CM4 排除 CM7 Application/Native Runtime 组，CM7 排除 CM4 组；不能按组“存在”判断实际编译。后续实验构建要显式列出有效 C 文件、组与文件级 IncludeInBuild、头路径、宏及 scatter 配置，保证一个 target 内没有两套 App_Init、任务入口、RuntimeEvent_Bind 或初始化导出。

暂不新增 V2 Target 或开关。切换须独立批准：只登记 V2 所需文件→排除旧业务入口→构建/Map 核验→功能验收→删除被替代文件和搜索路径。最终 `CM7/Application` 不留常驻旧架构；空目录可移除，既有 Media/Device/BSP 不随之删除。回滚使用完整旧 commit，而不是新旧混编。

## 5. R2条件澄清与R3设计依赖（输入54718ee）

Storage格式公开头建议只依赖Core/media_format.h和结果值类型，不直接使用CameraConfig_t、Graph私有定义或HAL。GraphFormat建议为CoreMediaFormat别名；Camera/UVC配置类型转换留Ports，Storage实际Frame/Lease承接也留其端口。上述路径和符号均为文档方案，未创建目录、修改include或工程登记。

R3新增资源架构/验收设计见ResourceOwnershipArchitecture与ResourceOwnershipAcceptance；D/T/消费证明尚未实施、两个NOT_PROVEN保持。Resource不拥有拓扑，Graph不拥有Buffer，不新增Resource Task。

## 6. 冻结路线落点同步（2026-11-17）

目录树与允许/禁止依赖逐项保持；阶段号不等于目录层号。Core/Configuration/MediaGraph基础实现归Phase1；Resource与Ports归Phase2；Lifecycle/Supervisor的执行协调归Phase3；Modules/Camera、USB、Storage分别归4/5/6；Control Service归7（复用既定内部目录，不新增顶层）；真实重配置归8；Network/AI归9；最终删除Legacy归10。树中早期未创建/未实施说明是历史设计状态，以501330a源码和Phase1审核为当前事实。
