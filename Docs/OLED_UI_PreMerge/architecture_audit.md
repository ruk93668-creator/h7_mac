# M1 架构审计

| 模块 | 实际职责与结论 |
|---|---|
| App_Oled | 唯一更新入口和刷新事务，获取 Service 快照、512 B 备份、绘制、提交及失败回滚。Timer 与完成后的单个延期请求使用同一 App 函数，运行一帧后刷新 Device 才能重新获得写权限 |
| Page 与 Input | Page 保存固定拓扑和选中状态；Input 将 U7 样本转成事件，8 项 FIFO 限制积压。在越过逻辑或局部视图边界后丢弃旧事件 |
| Anim / Popup | 只维护当帧采样的插值、滚动和单活动窗，无延时等待、无独立任务和中断写帧 |
| Menu / Compat | Menu 负责业务页与常规列表；Compat 保存 UI Tools 的视图、值草稿、字模和内置编辑器。父页仍由同一 Page 管理，二级视觉状态现已显式通知队列 |
| Status / System Monitor | 只读转换，不保存业务裸指针。CM7 诊断信封是尚未接线的本地模型，不直接访问共享地址 |
| Device / BSP / Framework / IPC | 与 main 逐文件一致。唯一的 Device Framebuffer 为 512 B，App 保留 512 B 回滚备份。无新增帧缓冲 |

Application 的直接 HAL 调用限于既有 U7 GPIO 初始化及采样。I²C、SSD1306 命令及刷新推进仍在原 Device/BSP。计时报表只在调试宏构建。引脚抽象重新分层和 IPC 数据接入均不在整理改动中。通过局部修复保留现有内存、调度和错误契约。
