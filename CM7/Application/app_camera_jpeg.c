#include "app_camera_jpeg.h"

#if APP_CAMERA_JPEG_TEST_ENABLED
#include "app_camera.h"
#include "app_messages.h"
#include "camera_capture.h"

//JPEG验收使用独立配置，不覆盖video_config默认业务配置
static const CameraConfig_t s_jpeg_config =
{
    .width = 160U,
    .height = 120U,
    .format = CAMERA_PIXEL_FORMAT_JPEG
};

//记录本次实板导出状态，不在默认UVC固件分配图像数组
volatile AppCameraJpegDebug_t g_camera_jpeg_test;

//仅CameraTask推进一次JPEG验收，默认固件不调用
void App_CameraJpegProcess(void)
{
    CameraFrame_t frame;
    SysResult_t result;

    if (!App_MessagesIsOwner(RUNTIME_TARGET_CAMERA))
        return;

    if (g_camera_jpeg_test.state == 0U)
    {
        result = App_CameraConfigure(&s_jpeg_config);
        if (result == SYS_OK)
            result = App_CameraInit();
        g_camera_jpeg_test.result = result;
        g_camera_jpeg_test.state = result == SYS_OK ? 1U : 3U;
    }
    if (g_camera_jpeg_test.state == 1U)
    {
        App_CameraRun();
        result = CameraCapture_GetLastResult();
        if (result != SYS_OK)
        {
            g_camera_jpeg_test.result = result;
            g_camera_jpeg_test.stop_attempted = 1U;
            g_camera_jpeg_test.stop_result = App_CameraStop();
            g_camera_jpeg_test.state = 3U;
            return;
        }
        if (CameraCapture_GetFrame(&frame))
        {
            g_camera_jpeg_test.frame_address = (uint32_t)frame.data;
            g_camera_jpeg_test.size_bytes = frame.size_bytes;
            g_camera_jpeg_test.width = frame.width;
            g_camera_jpeg_test.height = frame.height;
            g_camera_jpeg_test.state = 2U;
        }
    }

    //导出前保持Media READY，禁止重启采集覆盖文件
    if (g_camera_jpeg_test.state == 2U && g_camera_jpeg_test.release_request != 0U)
    {
        result = CameraCapture_ReleaseFrame();
        if (result == SYS_OK)
        {
            g_camera_jpeg_test.stop_attempted = 1U;
            result = App_CameraStop();
        }
        g_camera_jpeg_test.stop_result = result;
        g_camera_jpeg_test.state = result == SYS_OK ? 4U : 3U;
    }
}
#endif
