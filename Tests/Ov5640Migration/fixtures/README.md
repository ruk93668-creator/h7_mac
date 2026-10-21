# 历史ID探测夹具

正式Application中的临时Timer、10次循环和统计变量已按用户要求移除。其源码保存在此，供既有独立验收重现；本目录不加入Keil Group，不进入正式固件。

probe_test.c现在直接包含这些历史Application夹具，BSP、Device和Framework仍使用当前正式源码。原验收报告保留为历史记录，不能据此声称正式固件仍自动运行10次。

清理后正式Application无Timer/无主动读取的回归见Tests/Ov5640Cleanup。
