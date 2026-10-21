#include "app_camera.h"
#include "camera_capture.h"
#include "ov5640_device.h"
#include <stddef.h>
#include <stdbool.h>

//统一选择当前分辨率和像素格式
static const CameraConfig_t s_camera_config =
{
	.width  = 160U,
	.height = 120U,
	.format = CAMERA_PIXEL_FORMAT_YUV422
};

static bool s_camera_initialized;

const CameraConfig_t *App_CameraGetConfig(void)
{
	return &s_camera_config;
}

SysResult_t App_CameraInit(void)
{
	Device_t *device;
	Ov5640Device_t *camera;
	SysResult_t result;

	//初始化成功后不重复配置传感器和Media
	if ( s_camera_initialized)
		return SYS_OK;

	//获取自动注册的摄像头设备
	device = Device_Find("ov5640");
	if ( device == NULL)
		return SYS_ERR_NOT_FOUND;
	camera = FRAMEWORK_CONTAINER_OF(device, Ov5640Device_t, base);
	if ( camera->ops == NULL || camera->ops->configure == NULL)
		return SYS_ERR_NOT_SUPPORTED;

	//同一份配置用于传感器和Media
	result = camera->ops->configure(camera, App_CameraGetConfig());
	if ( result != SYS_OK)
		return result;
	result = CameraCapture_Init(App_CameraGetConfig());
	if ( result != SYS_OK)
		return result;
	
	//开机启动一次采集，失败时保留原错误
	result = CameraCapture_Start();
	if ( result != SYS_OK)
		return result;
	s_camera_initialized = true;
	return SYS_OK;
}

void App_CameraRun(void)
{
	//推进单帧采集，完整图像交给业务获取和释放
	CameraCapture_Process();
}
