# 实板与协议待验收项

| 项目 | 状态 | 证据要求 |
| --- | --- | --- |
| 候选 CM4 画面及 U7 交互 | NOT_RUN | 烧录候选 HEX，验证首页、菜单、工具页、全屏边缘和连续动画 |
| 稳定完成至少 20 FPS | NOT_RUN | 连续动画记录 10 s 以上的完成帧、错误计数和间隔，导出 I²C 波形并核对延期请求 |
| I²C 在途 Framebuffer | 候选实板 NOT_RUN | 比较四页传输期间的 Framebuffer，记录 WAIT_CMD、WAIT_DATA 与 IDLE |
| CM7 栈水位与实时影响 | NOT_RUN | 覆盖 Camera、USB、Storage 单路与组合负载，记录真实水位和顺序 |
| 首次黑屏 | 历史记录保留 | 用户反馈当时为操作问题，见 UI1 补充记录，集成候选尚无独立上电证据 |
| Storage 与系统监控真实数据 | 等待诊断协议审批 | 当前显示 N/A、缺失或失联 |
| OLED 控制 CM7 业务 | 等待控制协议审批 | 菜单只读，ControlTask 继续拥有 Supervisor |
