# Stage A 依赖边界专项

基线：48afa27a1ac941c541ac400a3a820474b25e65c3。只验证阶段A，不实现自主STOP。

## 执行

使用ARMCC5（默认 D:/32/ARM/ARMCC/Bin）、Python、Unicorn和pyelftools。

```powershell
python -m pip install --target "$env:TEMP/h7_stageA_48afa27/deps" unicorn pyelftools
python Tests/StageADependencyBoundary/run_tests.py
python Tests/StageADependencyBoundary/run_historical.py after
python Tests/StageADependencyBoundary/run_effective.py
python Tests/StageADependencyBoundary/run_build.py
```

输出在临时目录，不修改Keil Target或输出目录。历史脚本自身产生的已跟踪Tests文件会按执行前副本恢复。运行期间不要同时编辑或运行其他测试。

`run_tests.py`：40次真实Media/事件适配ARM执行（O0/O2）、4次真实CMSIS平台指令执行、2项错误范围的预期编译失败、9项源码逆替换等价检查。总55项不是55次实板验证。

`run_effective.py`：固定读取本地Git提交3083561中已有的生产兼容测试。创建临时源代码只读用途junction，执行当前工作区源码；只补新增Platform链接及CPU测试桩类型，不改变功能断言。它需要该Git对象存在。46个帧租借/AVI功能场景×O0/O2=92，60个FatFS功能场景×O0/O2=120，44个真实BSP/HAL IDMA场景×O0/O2=88。归档生产适配器原本排除了已删除的耗时探针场景；已移除的SdTest/压力入口不恢复、不计为PASS。

`run_historical.py`：原24组脚本逐组执行，结果包含历史失败和夹具缺失；不能把退出码1的套件算全通过。`historical_comparison.json`保存基线和本次逐例结果。阶段A逆替换检查针对固定基线，不是未来任意版本的通用CI。

## 边界

CPU替身检查延后中断与通知，真实CMSIS机器码检查MRS/MSR/CPSID等指令效果。Unicorn不能证明真实中断延迟、屏障的物理总线效果、FMC竞争或实板帧率。范围负向编译通过意味着错误配置被原断言拒绝，不是固件编译失败。

生产固件没有加入任何专项入口、探针、验收任务或测试宏。
