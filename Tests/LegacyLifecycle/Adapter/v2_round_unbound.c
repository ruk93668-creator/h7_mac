#include "../../../CM7/ApplicationV2/Core/Runtime/v2_rtos_runtime.h"

// 历史夹具只运行 Legacy CameraTask；P8.2 端口与队列未绑定，轮次钩子不执行业务。
void V2Runtime_Round(RuntimeEventTarget_t task, uint32_t events, uint32_t now)
{
    (void)task;
    (void)events;
    (void)now;
}
