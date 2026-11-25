# 生产源码审核

扫描范围为实际候选中的 App、UI、Assets 和 BSP U7 文件。扫描阶段命名、专用输入邮箱、DWT、模拟及故障注入标志，均为零。HAL 访问仅出现在 BSP。逐文件的中文注释行数供人工复核，数量不代替语义检查。

| 文件 | 职责 | 注释规范 | 测试标识 | 功能变化 | 结果 |
| --- | --- | ---: | ---: | --- | --- |
| `CM4/Application/app_oled.c` | 执行刷新与回滚、调度、读取心跳及协调输入 | 60 行中文单行注释 | 0 | 心跳/裁剪适配 | 已核对 |
| `CM4/Application/app_oled.h` | 执行刷新与回滚、调度、读取心跳及协调输入 | 16 行中文单行注释 | 0 | 心跳/裁剪适配 | 已核对 |
| `CM4/BSP/bsp_u7.c` | 沿用板级引脚及 LSE 保护 | 3 行中文单行注释 | 0 | 组件职责迁移 | 已核对 |
| `CM4/BSP/bsp_u7.h` | 沿用板级引脚及 LSE 保护 | 5 行中文单行注释 | 0 | 组件职责迁移 | 已核对 |
| `CM4/Application/UI/Assets/oled_ui_assets.inc` | 上游字体字模 | 10 行中文单行注释 | 0 | 组件职责迁移 | 已核对 |
| `CM4/Application/UI/Assets/oled_ui_bitmaps.inc` | 16×16 图标和图像动画帧 | 4 行中文单行注释 | 0 | 组件职责迁移 | 已核对 |
| `CM4/Application/UI/oled_ui.c` | 原四行 IPC 监控和基础 ASCII 字格 | 46 行中文单行注释 | 0 | 组件职责迁移 | 已核对 |
| `CM4/Application/UI/oled_ui.h` | 原四行 IPC 监控和基础 ASCII 字格 | 3 行中文单行注释 | 0 | 组件职责迁移 | 已核对 |
| `CM4/Application/UI/oled_ui_anim.c` | 动画状态及弹窗逻辑 | 8 行中文单行注释 | 0 | 组件职责迁移 | 已核对 |
| `CM4/Application/UI/oled_ui_anim.h` | 动画状态及弹窗逻辑 | 25 行中文单行注释 | 0 | 组件职责迁移 | 已核对 |
| `CM4/Application/UI/oled_ui_draw.c` | 图元绘制、裁剪与写入权限 | 12 行中文单行注释 | 0 | 组件职责迁移 | 已核对 |
| `CM4/Application/UI/oled_ui_draw.h` | 图元绘制、裁剪与写入权限 | 13 行中文单行注释 | 0 | 组件职责迁移 | 已核对 |
| `CM4/Application/UI/oled_ui_font.c` | 多尺寸字模和裁剪 | 8 行中文单行注释 | 0 | 心跳/裁剪适配 | 已核对 |
| `CM4/Application/UI/oled_ui_font.h` | 多尺寸字模和裁剪 | 3 行中文单行注释 | 0 | 心跳/裁剪适配 | 已核对 |
| `CM4/Application/UI/oled_ui_input.c` | 相位解码、按键去抖和事件队列 | 7 行中文单行注释 | 0 | 组件职责迁移 | 已核对 |
| `CM4/Application/UI/oled_ui_input.h` | 相位解码、按键去抖和事件队列 | 17 行中文单行注释 | 0 | 组件职责迁移 | 已核对 |
| `CM4/Application/UI/oled_ui_menu.c` | 菜单、状态和活动弹窗绘制 | 6 行中文单行注释 | 0 | 组件职责迁移 | 已核对 |
| `CM4/Application/UI/oled_ui_menu.h` | 菜单、状态和活动弹窗绘制 | 2 行中文单行注释 | 0 | 组件职责迁移 | 已核对 |
| `CM4/Application/UI/oled_ui_page.c` | 固定页面表及导航 | 19 行中文单行注释 | 0 | 组件职责迁移 | 已核对 |
| `CM4/Application/UI/oled_ui_page.h` | 固定页面表及导航 | 23 行中文单行注释 | 0 | 组件职责迁移 | 已核对 |
| `CM4/Application/UI/oled_ui_status.c` | 应用诊断与本地系统监控模型 | 6 行中文单行注释 | 0 | 心跳/裁剪适配 | 已核对 |
| `CM4/Application/UI/oled_ui_status.h` | 应用诊断与本地系统监控模型 | 48 行中文单行注释 | 0 | 心跳/裁剪适配 | 已核对 |
| `CM4/Application/UI/oled_ui_tools.c` | 工具视图、编辑、组合法及交互效果 | 24 行中文单行注释 | 0 | 组件职责迁移 | 已核对 |
| `CM4/Application/UI/oled_ui_tools.h` | 工具视图、编辑、组合法及交互效果 | 14 行中文单行注释 | 0 | 组件职责迁移 | 已核对 |
