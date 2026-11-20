#include "../../Core/app_context.h"
#include "camera_capture.h"
#include <stddef.h>

//仅 CameraTask 写入已验证的配置引用。
static const CameraConfig_t *s_camera_config;

SysResult_t AppV3_CameraInit(const AppV3Context_t *context)
{
    SysResult_t result;

    if (s_camera_config != NULL)
        return SYS_ERR_ALREADY_EXISTS;
    if (context->video == NULL)
        return SYS_ERR_INVALID_STATE;

    //复用采集验证器，暂不初始化传感器、缓冲池或 DCMI。
    result = CameraCapture_ValidateStreamConfig(&context->video->camera);
    if (result == SYS_OK)
        s_camera_config = &context->video->camera;

    return result;
}
