# 中文注释审核

迁移文件保留原 GBK 编码与 CRLF。宏说明注明时间、容量和范围，状态字段区分调度时刻、实际完成时刻、物理输入和有界草稿。新增说明使用中文单行 `//`。许可证和来源声明保留原文。

| 原代码 | 整理后 |
| --- | --- |
| `s_ui2_state` | `//保存当前页面及各页独立游标`，`s_page_state` |
| `OLED_UI3_REVERSE` | `//U7方向配置，置1可交换顺时针与逆时针事件`，`APP_OLED_U7_REVERSE` |
| `uint32_t received_ms` | `uint32_t received_ms; //CM4本地接收时间，单位ms` |
| 单一兼容模块混用字体与工具状态 | Font 独立说明裁剪边界，Tools 保留编辑与视图语义 |

逐文件职责与核查见 [生产源码审核](production_source_audit.md)。
