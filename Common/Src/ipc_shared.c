#include "ipc_shared.h"

__attribute__((section(".shared")))
volatile IpcShared_t g_ipc;
