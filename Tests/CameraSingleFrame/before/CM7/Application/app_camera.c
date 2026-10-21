#include "app_camera.h"
#include "ov5640_device.h"
#include <stddef.h>

//统一选择当前分辨率和像素格式
static const CameraConfig_t s_camera_config =
{
	.width  = 160U,
	.height = 120U,
	.format = CAMERA_PIXEL_FORMAT_Y8
};

const CameraConfig_t *App_CameraGetConfig(void)
{
	return &s_camera_config;
}

SysResult_t App_CameraInit(void)
{
	Device_t *device;
	Ov5640Device_t *camera;
	SysResult_t result;

	//获取自动注册的摄像头设备
	device = Device_Find("ov5640");
	if ( device == NULL)
		return SYS_ERR_NOT_FOUND;

	camera = FRAMEWORK_CONTAINER_OF(device, Ov5640Device_t, base);
	if ( camera->ops == NULL || camera->ops->configure == NULL)
		return SYS_ERR_NOT_SUPPORTED;

	//将同一份配置传给Device，保留现有彩条输出
	result = camera->ops->configure(camera, App_CameraGetConfig());
	if ( result != SYS_OK)
		return result;
	
	return SYS_OK;
}
