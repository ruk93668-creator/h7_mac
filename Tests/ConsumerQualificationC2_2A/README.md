# C2.2A Consumer Qualification Core

纯核心67个场景及真实App_Camera适配14个场景，ARMCC5 O0/O2共162次。使用真实C1/C1.1/C2.1.1，不复制Replay。资源和归还证据均明确为桩，生产证据未绑定。

- [实施报告](../../Docs/Design/C2.2A_ConsumerQualificationCore实施报告.md)
- [专项结果](results.json)、[Q映射](q_mapping.json)
- [原协议回归](protocol_results.json)、[历史回归对照](regression_results.json)
- [正式构建](build_results.json)、[ABI](layout_results.json)、[范围和Map审计](scope_memory_audit.json)

复现顺序见实施报告第11节。构建产物和隔离快照只保留在output/，不修改Keil输出设置，不写卡、不自动运行资格业务。当前项目仍运行原Camera/USB/Storage流程，不能把专项PASS当作自主生命周期实板通过。
