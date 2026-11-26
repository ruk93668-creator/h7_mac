# N5 JPEG 尺寸验收工具修复

状态：**READY_FOR_REVIEW**。日期：2026-11-26。所有新增验证为主机/回环验证，实板 NOT_RUN。

## 基线与范围

从 codex/ethernet-bringup@21ac67f2a119d651868b83d3f9315b67c241f1e3 创建独立分支 codex/ethernet-n5-tool-fix。
修复源码提交：f9e17cf3c58cfa911cab69178399c9827bd97a42。后续证据提交只包含文档与日志；不修改原 Ethernet 分支、main 或原工作区。
Firmware 继续使用原冻结源码 78580073610ff4cfe06a4b004b1b0d75bffef499 的已验收构建，不重新烧录。
本轮未修改生产源码、Keil、MPU/DMA/Cache、TCP 参数、任务资源或 ApplicationV3 生命周期。

修改：
- Tests/Ethernet/e6_peer.py：--width 默认160、--height 默认120，整型范围1..65535。
- Tests/Ethernet/test_e6_peer.py：保留旧三项测试，增加六项测试方法及多组负例。
- Tests/Ethernet/e6_peer_fixtures/qvga.jpg、README.md：可实际解码的320×240测试JPEG，记录生成方式及SHA。
- N5_HardwareAcceptance.md：三条真实摄像头调用全部显式增加 --width 320 --height 240。
- 本报告与 Evidence/N5ToolFix：只新增证据，不回写历史失败。

## 校验与 JSON 契约

read_frame 原有两个位置参数和二元返回值兼容；measure 未传 width/height 的旧调用也兼容。
严格保留原边界、Content-Length、CRC、序号、FIN/RST、八张fixture差异和FPS断言。
尺寸从JPEG SOF取得，与用户期望精确比较；增加SOF分量记录长度及零尺寸检查。
没有通过自动接受实际尺寸放宽断言。工具检查传输CRC和JPEG结构/SOF，不替代完整熵解码器。

JSON 原 status/routes/sessions 及 frames/fps/jpeg_bytes/unique_frames 等字段保留。
新增 expected_resolution={width,height}；jpeg_validation 按尝试会话记录：
status、frames_checked、crc_verified_frames、observed_dimensions，失败时记录error。
CRC有效但分辨率错误时，保留实际尺寸、CRC计数、FAIL和进程退出码1。
参数非法在连接网络/创建结果文件前退出2。既有输出文件继续使用独占创建，不覆盖历史结果。

## 测试结果

9/9 unittest方法通过；表中负例PASS指断言成功捕获失败，不表示坏帧通过。

| 场景 | 工具预期 | 实际断言 |
|---|---|---|
| 默认160×120，旧八张Fixture | PASS | PASS，8帧及CRC/字节数/FPS字段正确 |
| 显式320×240 | PASS | PASS，8帧及CRC/字节数/FPS字段正确 |
| 期望320×240，实际160×120 | FAIL | 退出1，FAIL及160×120被记录 |
| 期望160×120，实际320×240 | FAIL | 退出1，FAIL及320×240被记录 |
| 负载字节损坏，原CRC不变 | FAIL | CRC mismatch，CRC成功计数0 |
| SOF过短/超长、零宽高、分量错误、SOF缺失/截断、EOI缺失 | FAIL | 全部拒绝，即使重算CRC也拒绝 |
| 原multipart长度/序号/边界/CRC/截断负例 | FAIL | 原测试保留并通过 |
| width/height为0、负数、65536、小数、非数字 | 参数错误 | 各退出2，未连接网络 |
| FIN、RST、重连 | 原断言 | 原回环测试通过 |

原版E6默认CLI未使用--camera绕过八张fixture检查。N5路径使用--camera及显式尺寸。
四份真实工具JSON存于Evidence/N5ToolFix；地址127.0.0.1，仅模拟服务端，不是板卡。
旧E6真实lwIP模拟MAC回归PASS；E7动态源和E7 QVGA相关回归PASS，原严格编译/WX保持。
虚拟605秒/续租等输出是软件模拟时钟，不能作为实板长稳或实测续租。

复现：
```powershell
python -B -m unittest discover -s Tests/Ethernet -p test_e6_peer.py -v
# 在 x64 VS 开发命令环境执行：
python -B Tests/Ethernet/run_e6_host.py
python -B Tests/Ethernet/run_e6_host.py --dynamic
python -B Tests/Ethernet/run_e6_host.py --dynamic --qvga
```

新增CLI测试初次失败是本机单批收取8帧时monotonic计时跨度为0，导致测试要求FPS>0失败。
first-unit-failure.log保留。只给模拟CLI加入--slow-ms 5形成可观测时间跨度，未改生产FPS算法或降低断言。
历史N1–N5失败、IPC Status基线FAIL和原交付ZIP保持原样。

## 生产不变证据与交付

production-unchanged.json记录CM7、CM4、Common、Platform、ThirdParty、MDK-ARM等Git目录树在基线与修复提交完全一致。
原工作区RTE SHA256仍为270b61dded9e5eeb243f633224ced83b1c81bc11f38abf5bc68e9d03d072ba8d。
本轮不重复固件编译；新版包沿用原AXF/HEX/MAP，逐文件哈希证明固件未变化。
完整工具修复Diff、文件SHA、旧E6及相关回归原始日志随附。

新包另外生成，不覆盖Ethernet_N1_N5_78580073.zip；其原SHA为
81b883809b8f84d2e9329491e32acf34668772c41ad632d6fd453e5130903b99。
新版包替换使用说明，旧版说明另存Archive；包含修复工具/Fixture/测试和新证据。
包内package-manifest.json列出所有文件SHA，外部package.json记录新版ZIP SHA，避免自引用。
版本和生产不变结论针对明确源码提交，最终文档提交只补充交付证据。

未执行实板：下载器Verify、MPU/DCache读回、真实摄像头、100轮、60分钟多外设均NOT_RUN。
DMA FIFO Error、20FPS及实板验收风险不因工具修复关闭。
完成后停止等待独立审核，不合并任何分支。
