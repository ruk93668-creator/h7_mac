//网络 API 所有权由 Application 绑定，平台层不持有 RTOS 句柄。
#ifndef ETH_EXECUTION_H
#define ETH_EXECUTION_H

typedef int (*EthExecutionCheck_t)(void *context);

//由启动拥有者绑定一次。回调与 context 常驻，不允许替换或解除。
//返回 1 表示绑定成功，0 表示参数无效、重复绑定或不在指定上下文。
int eth_execution_bind(EthExecutionCheck_t check, void *context);

//绑定后必须由指定普通任务调用。回调同时拒绝 ISR 和禁止调度的上下文。
//未绑定供独立单循环固件使用，正式 Service 在发布前必须绑定。
int eth_execution_allowed(void);

#endif
