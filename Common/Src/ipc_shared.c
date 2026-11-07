#include "ipc_shared.h"

//只预留共享对象，各核通过协议入口初始化自己的区域
__attribute__((section(".shared"), aligned(32)))
volatile IpcShared_t g_ipc;
