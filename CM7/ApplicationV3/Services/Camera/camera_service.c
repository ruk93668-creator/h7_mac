#include "../../Core/app_context.h"
#include "camera_capture.h"
#include <stddef.h>

//仅 CameraTask 写入已验证的配置引用。
static const CameraConfig_t *s_camera_config;

//校验采集配置并保存已验证的只读引用
SysResult_t AppV3_CameraInit(const AppV3Context_t *context)
{
    SysResult_t result;

    //服务只接受一次启动配置，保留已有采集参数
    if (s_camera_config != NULL)
        return SYS_ERR_ALREADY_EXISTS;
    if (context->video == NULL)
        return SYS_ERR_INVALID_STATE;

    //复用采集验证器，暂不初始化传感器、缓冲池或 DCMI。
    result = CameraCapture_ValidateStreamConfig(&context->video->camera);
    //只有采集验证器接受的配置才能发布
    if (result == SYS_OK)
        s_camera_config = &context->video->camera;

    return result;
}
