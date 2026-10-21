#ifndef CM7_IPC_SERVICE_H
#define CM7_IPC_SERVICE_H

#include "sys_result.h"

//唤醒CM4前初始化共享区，最后发布有效标志
void IPC_Service_EarlyInit(void);
#endif /* CM7_IPC_SERVICE_H */
