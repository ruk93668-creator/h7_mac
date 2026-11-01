# UVC-06主线集成

基线main/origin/main：`8abc24530a7b7d55953eed797f11ee16134ea719`。

集成内容：34字节UVC 1.1 Probe/Commit、通用EP0 STALL保护、跨请求完成校验、VC/VS标准GET_STATUS，以及全部对应测试。生产修改仅CM7/Service/usb_uvc_class.c、CM7/Platform/usb_device.c。

用户已批准按软件验收合并main。本次在codex/uvc-06-integration检查后创建提交，仅快进合并并普通推送，不强制推送、不改写历史。实际提交SHA以本文件所属Git提交及最终交付报告为准。

## 保留用户修改

正式工程原有16行UvcStreamingControl_t草稿为旧26字节字段定义。原文件及原始diff已原样备份到仓库外`C:\Users\29062\.codex\worktrees\uvc-06\uvc06_main_merge_backup`。逐字段核对其11个字段类型、名称全部包含于新的16字段34字节定义中；本次由完整验收结构替代，不留下重复typedef。新结构不使用sizeof直接传输，显式编解码。

原output/、tmp/未提交文件保持原样，并对合并前后的原有文件SHA进行检查。禁止提交这些输出目录、构建产物、Python依赖或凭据。

## 软件验收

合并前重新执行全部六套测试，并完整重编CM7、CM4。详细结果见regression_results.json：原377项全部通过，新增39项全部通过，共416/416；两核0 Error、0 Warning。

运行入口：run_tests.py（89项）、run_cancellation_tests.py（39项）、原USB Core（204）、EP0 IRQ（4）、UVC Lifecycle（40）、Descriptor（40）。原测试没有删除；直接调用Class Setup的夹具同步保存真实Core请求现场。

## 历史材料与长期测试

verification_report.md记录整改前24项失败及整改后结果。cancellation_before.json是修复前历史证据，其他当前results.json记录已修复结果。audit.json、final_audit.json是对应阶段冻结的审计快照，其中未提交状态及旧HEAD属于生成时的事实，不作为可重复执行的CI检查。

合并前一次性stdout日志及final_review.patch不加入正式仓库。保留可运行测试源码/脚本、命令、结构化结果与两核构建日志。编译/测试脚本仍依赖本机ARMCC5、Keil及Unicorn环境，不宣称可直接用于任意CI主机。

## 未实板验收

没有宣称Windows UVC协商/枚举、总线STALL波形或视频传输通过。没有打开EP1、实现Alt1/Payload或PTS/SCR采样；USB Mode仍保持原TEST逻辑。描述符、BSP、HAL/LL、官方ST Core、Camera、Media和CM4源码未修改。
