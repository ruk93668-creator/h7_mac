# Ethernet 代码风格校准

依据主线 Tests/CameraStyleCalibration/校准报告.md：Tab 缩进、Allman 花括号、展开压缩语句、结构体和枚举展开、简短中文 // 模块/函数/关键逻辑注释。生产源码和测试 C/H 共 14 个文件非注释 C Token 完全一致；公开名称、接口、状态转换和内存布局不变。保留已有 UTF-8 编码，主机 MSVC 明确设置 /utf-8，避免按系统代码页解释中文注释。

局部 .clang-format 只作用于 Ethernet 目录，不影响其他模块。六项本机测试通过，完整 CM7 smoke 固件 0 Error / 0 Warning，Code/RO/RW/ZI 和整理前一致。证据见 Tests/Ethernet/style_results.json。风格整理后尚未重新实板验收；原实板基础收发记录属于整理前版本。
