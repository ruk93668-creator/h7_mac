//正式网络服务复用 ApplicationV3 的启动屏障、Record 和 Mailbox。
#ifndef APP_V3_NETWORK_SERVICE_H
#define APP_V3_NETWORK_SERVICE_H

#include "app_context.h"
#include "../../Runtime/service_mailbox.h"

//只有 NetworkTask 可以初始化、轮询和清理协议栈。
//初始化只发布软件服务，不把 PHY Link 或 DHCP 租约作为启动屏障。
SysResult_t AppV3_NetworkInit(const AppV3Context_t *context);
//仅用于 Record 发布前失败的回收，不代表运行中的网络停止。
void AppV3_NetworkDiscard(void);
//Registry 登记的常驻网络任务入口。
void AppV3_NetworkRun(void *argument);
//网络任务逐轮处理邮箱和资源，返回有界等待毫秒数。
uint32_t AppV3_NetworkAdvance(void);

#endif
