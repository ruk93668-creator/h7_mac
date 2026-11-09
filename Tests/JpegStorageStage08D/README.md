# Stage08D

设计：../../Docs/Design/Stage08D_JPEG分发与SD录像设计.md 。结果：verification_report.md 。实板：hardware_steps.md。

需要ARMCC5、Keil、pyelftools0.33、Unicorn2.1.4；本机依赖可通过PYTHONPATH使用，不提交运行库。PC解码检查需要FFmpeg/ffprobe位于PATH。

```powershell
python Tests/JpegStorageStage08D/run_tests.py
python Tests/JpegStorageStage08D/run_bridge.py bridge_test --count 35
python Tests/JpegStorageStage08D/run_media.py media_test --count 90
python Tests/JpegStorageStage08D/run_tasks.py
python Tests/JpegStorageStage08D/validate_avi.py
python Tests/JpegStorageStage08D/run_build.py
python Tests/JpegStorageStage08D/build_profiles.py
python Tests/JpegStorageStage08D/audit.py
```

run_tests编译真实应用、Dispatcher、Bridge、AVI、JPEG验证，仅硬件与文件设备在RAM模拟。Bridge复用原28项断言；Media复用原90项五缓冲断言，误走JPEG桩即失败。真实内核测试在等待API处停止观察，不冒充NVIC完整调度仿真。

fixtures/sample.jpg是固定合成320x240 RGB梯度、JPEG质量70样本，非硬件采集；fixture.json记录SHA。70000B用例加入合法COM段，验证容量和分块写。validate_avi独立检查RIFF/索引并实际解码软件输出。

build_profiles不改工程设置，只重编配置并重新链接同源对象。audit依赖Git基线41a4ba4和本轮构建输出；浅克隆需获取基线，未构建不能将审计缺少AXF误判成产品回归。旧14套有效回归入口仍是Tests/SdmmcStage08B/run_regressions.py；保留历史不兼容维护记录，不虚报全仓全部通过。
