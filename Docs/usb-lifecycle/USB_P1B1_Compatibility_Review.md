# P1-B.1 五项旧同步契约人工审核表

VERIFIED：原 `Tests/UsbCoreDecoupling/endpoint_lifecycle_test.c` 五项断言完整保留。基线 5113000 已有五项 FAIL；不能以新增测试 PASS 宣称这五项已绿。本轮不批准旧测试迁移，以下 CONTRACT_CHANGED 均等待人工审核。

| Case / 原测试位置 | 原目标及旧实现 | 当前实现及差异 | 安全风险与分类 | 建议及审批状态 |
| --- | --- | --- | --- | --- |
| 334 / :468 | 活跃 IN3/OUT3 同时占用；IN Close 的 HAL Close 失败后 IN opened=true、busy=false，OUT opened/busy=true；旧路径先同步 Abort 归还 IN | 当前活跃 Bulk IN 在 Abort 入口返回 BUSY，尚未进入 HAL Close，IN/OUT 资源都保留 | CONTRACT_CHANGED。未取得 Stop Proof 时恢复旧 busy=false 会提前归还 IN。OUT 不能因 IN 关闭失败被释放 | 保留原 FAIL；新增契约要求先完成 Begin/Poll/Commit，再独立测试 Close 失败。等待审批迁移 |
| 335 / :477 | 模拟 HAL Transmit BUSY；原实现清 busy 后立即重试 | 失败提交保守保留 source/pool/Transfer；禁止盲目重试 | CONTRACT_CHANGED。真实 HAL_PCD_EP_Transmit :1884–1904 先写 xfer 上下文、调用 LL，最终返回 HAL_OK，没有该 HAL_BUSY 分支。夹具 fault=9 并非实际硬件取消证明。不能推广“BUSY必然无副作用” | 保留原 FAIL。若将来提供可证明零副作用的 NOT_SUBMITTED 分支，另建明确结果类型；本轮没有此证明，不释放 |
| 361 / :705 | 活跃 Bulk Abort 同步成功，busy=false，可立刻重用 | 同步接口仅表达未完成，返回 BUSY；Task 驱动分阶段 Stop Proof 后 Commit | CONTRACT_CHANGED。为了旧断言在 ISR 等待完整流程会违反上下文契约 | 保留 FAIL；Task 契约专项验证取得证明后才重用。等待审批迁移 |
| 362 / :714 | 注入 HAL Abort ERROR 保留 opened/busy，清 fault 后再次同步 Abort 成功 | 活跃 Bulk 不走旧 HAL Abort，仍保留原 Transfer/Buffer；失败收尾只可原 Attempt Reconcile | CONTRACT_CHANGED。清软件 fault 不证明硬件静止，HAL_ERROR不承诺未来回调 | 保留 FAIL；新测试验证原身份、首次错误、迟到 EPDISD/Flush 完成、失败引用保留。等待审批迁移 |
| 364 / :735 | Close 内 HAL Abort ERROR，不 Flush/Close，清 fault 重试即可成功 | Close 不可跨越 fence/未完成 Stop，返回 BUSY，不调用 HAL Close | CONTRACT_CHANGED。盲目重试 Close 会绕过端点事件退休及 FIFO owner | 保留 FAIL；新测试验证 Close/Abort 不释放，完整收尾后才可 Close。等待审批迁移 |

## 真实调用者与兼容限制

VERIFIED：`CM7/Service/usb_msc_bot.c:98` 为 Bulk IN 提交调用者；`:271–272` reset/cancel 路径调用 Abort；`:371–372` 在恢复处理中等待同步 Abort 返回成功。`CM7/Service/usb_msc_class.c:95,102` 关闭 OUT/IN。以上生产文件未修改。主动 Platform Stop 的单步协调不等价于这些 Class 内同步调用者全部完成迁移，MSC 整机恢复仍 UNRESOLVED。

VERIFIED：UVC `CM7/Service/usb_uvc_class.c:387,454,1016` 使用 ISO Abort/Transmit/Close，类型独立，未改协议实现。其历史回归仍保留失败，不能宣称 UVC 整机 PASS。

## 结果区分

VERIFIED：SYS_ERR_BUSY 表示当前操作未完成或不能执行，单独不代表异步取消已提交。HAL_ERROR 映射 SYS_ERR_IO，是失败事实，不代表稍后一定出现取消回调。只有 Platform 本次注册的 Stop 事务才允许 Service 投影为 Pending。

VERIFIED：本轮没有归类为 IMPLEMENTATION_DEFECT 后通过改旧断言制造绿的项目。Service 旧 BUSY→ERROR 的实现缺陷已经在授权文件修复；这不改变上述五项旧同步测试结果。335 的夹具与真实 HAL 不同已单独说明，但不以 TEST_FIXTURE_DEFECT 标签掩盖未经批准的公共契约变化。

UNRESOLVED：新旧接口的 Class 消费者迁移需单独批准；本轮禁止修改 MSC/UVC Class。人工审核前禁止删除、改期望或屏蔽这五项。原始结果及逐用例对照见最终验证 JSON。
