#ifndef CM7_APP_CAMERA_JPEG_H
#define CM7_APP_CAMERA_JPEG_H

#include "camera_config.h"

#ifndef APP_CAMERA_JPEG_TEST_ENABLED
#define APP_CAMERA_JPEG_TEST_ENABLED 0
#endif

#if APP_CAMERA_JPEG_TEST_ENABLED
#if !defined(APP_VIDEO_TEST_MODE) || APP_VIDEO_TEST_MODE != 1
#error JPEG acceptance requires explicit USB TEST mode
#endif

typedef struct
{
    //启动、采集和停止分别记录，恢复失败不伪装成功
    SysResult_t result;
    SysResult_t stop_result;
    uint32_t stop_attempted;

    //可导出文件的SDRAM起点和真实JPEG字节数
    uint32_t frame_address;
    uint32_t size_bytes;
    uint32_t width;
    uint32_t height;

    //0待启动、1采集中、2待导出、3失败、4已释放
    uint32_t state;

    //导出完成后由调试器置1，CameraTask再释放并停止传感器
    uint32_t release_request;
} AppCameraJpegDebug_t;

extern volatile AppCameraJpegDebug_t g_camera_jpeg_test;

//仅CameraTask推进一次JPEG验收，默认固件不调用
void App_CameraJpegProcess(void);
#endif
#endif
