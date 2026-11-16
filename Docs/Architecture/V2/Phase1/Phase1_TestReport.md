# Phase 1 测试报告

## 1. 方法与实际结果

原始输入为 ce33d07；只编译新增实际 V2 C 实现及独立 Mock 夹具。ARMCC5 5.06 update 7 build 960，Cortex-M7.fp.sp，C99，O0/O2；Unicorn 2.1.4 执行目标指令，pyelftools 0.33 读取 ELF。不是主机原生 C 行为模拟，也不是真实板卡执行。

最终 **125 个独立 ID × 2 优化等级 = 250/250 PASS，0 FAIL**。每条记录含优化等级、返回值、首个断言失败行、执行异常；原始数据在 `Tests/V2Phase1/results.json`。负向输入的 PASS 表示按断言拒绝，并未把拒绝当作业务启动成功。

| 组 | 每优化等级 | O0/O2合计 | 核心断言 |
|---|---:|---:|---|
| Core | 21 | 42 | ID/Status/错误、格式布局、无效参数、ARM ABI |
| Registry | 17 | 34 | 查询/Provider、重复 ID、范围/指针/版本/flag及各容量上限 |
| Resolver | 21 | 42 | 单/双消费 Mock 组合、闭包、歧义/pin、冲突/环、Optional、稳定排序/上限/失败 out 不变 |
| Graph + Format | 53 | 106 | Catalog、路径/fan-out/合并、类型/协议、互斥、依赖、rate、否决、回滚、最大容量、重入、capture-only |
| Composition | 13 | 26 | 三种组合、绑定一致性、无资源检查拒绝、资源否决、候选及诊断、重入 |

完整 ID→名称→源码行与 O0/O2 结果见 `Tests/V2Phase1/CaseIndex.csv`；不是只按汇总数字验收。ID 51 额外重复 Resolver 100 次；88 重复 Graph 25 次；99 重复 Composition 10 次，均使用相同调用者固定对象，不产生动态内存。这些重复不另膨胀独立测试 ID 数。

## 2. 重要断言

- 缺少、冲突、循环、多个不确定 Provider 不能进入候选；显式 preferred 才允许选定。Optional 已选 Provider 的依赖顺序及 AUTO 缺失计数单独验证。
- Fan-out 不操作 Frame。Raw/JPEG 固定 Pipeline，合法但不相同的 JPEG payload 配置拒绝合并；Raw/JPEG 互斥拒绝；独立 Sink 10/25 目标可以表达，但无实际 FPS 结论。
- Source/Sink/Pad/protocol、上限及未来声明严格拒绝；Capture/Provider/FrameRoute/Consumer 内部协议错误有独立负向用例。
- 精确 validator 返回 -123 及其 expected=123 诊断原样保留，不被 Graph 改写为成功。
- 失败时 out 的预置字节不变；主要别名及同 scratch 重入拒绝。公共 precondition 仍要求所有传入 C 对象有效且互不重叠，测试不构成对任意非法地址的安全保证。
- Resource Validator 缺失或否决均阻止 Composition；真实 Resource Manager 未实现，未生成任何资源安全 PASS。
- ARM 实测 AppMessage_t=52B、LifecycleMessage=56B、CameraFrame_t=24B；旧消息定义未变。AppMessage.frame offset=28、Lifecycle.identity offset=20；V2Status=4B。

## 3. 构建与原始证据

最终 CM7/CM4 使用原 Keil 工程完整 Rebuild，均退出 0、0 Error、0 Warning。V2 全部四个 C 在独立测试构建中两档编译/链接无诊断；原工程无 V2 运行绑定。

复现（仓库根目录）：

```powershell
python Tests/V2Phase1/run_tests.py
python Tests/V2Phase1/run_build.py
```

依赖安装到被忽略的本地 `output/V2Phase1/python_deps`，不提交依赖运行库。首次运行可用 `python -m pip install --target output/V2Phase1/python_deps unicorn==2.1.4 pyelftools==0.33`。ARMCC/UV4 路径沿用本机 `D:/32`。run_tests 借用历史 C1 的 ARM 执行 helper，但没有修改该文件或运行其历史测试主体。

最终原始 stdout、编译/链接命令与退出码、两核日志、构建 JSON/镜像 hash 在 `Tests/V2Phase1/Evidence/`。AXF/MAP/HTM/HEX 按原 Target 设置保存在本地 `output/V2Phase1/build_*/artifacts/`，遵守 gitignore 不提交二进制产物。目标未开启的 HEX 未强行打开。旧 MDK 输出在重编前复制到独立 previous_output；未清理任何原 output。

## 4. 开发期间的失败记录

首轮 Keil CM7 出现 4 条新增 enum 与 0 比较警告；已改为固定宽度 Status 和无无效下界比较，原日志 `Evidence/first_rebuild_warnings.log` 保留。CM7 新组导致 CM4 隐式编译的发现记录在实施报告，最终工程恢复原字节，最终日志证明不包含 V2 文件。

新增 ABI 探针最初误设 AppMessage.frame offset 为 32；ARM 运行断言失败。实际字段布局/sizeof 证明是 28，修正的是本轮新夹具的错误预期，旧测试断言未动；首次失败 stdout 和测量数据保存在 `abi_probe_initial_failure.log`、`abi_probe_measured.json`。最终按实际 28 明确断言并通过两档。这不是生产 ABI 改动，也没有删除历史失败。

## 5. 未执行与验收边界

本轮未复跑 Legacy B1/B2/B3、C1/C1.1/C2.1.1 的历史业务测试；生产代码、历史测试和配置没有变更，以范围检查及原工程完整构建验证编译兼容性，不把旧结果冒充本轮运行结果。真实 Camera/USB/Storage 功能、硬件故障、DMA、Grant/Lease 归还、72 项 R3、未来网络均 NOT_RUN。两个资源 NOT_PROVEN 保持。Phase 1 只形成可测试的候选计算基础，不构成硬件验收或 Phase 2 授权。
