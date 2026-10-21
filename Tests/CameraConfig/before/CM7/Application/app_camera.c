#include "app_camera.h"
#include "ov5640_device.h"
#include <stddef.h>

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
	if ( camera->ops == NULL || camera->ops->configure_y8_colorbar == NULL)
		return SYS_ERR_NOT_SUPPORTED;

	//检查芯片ID并配置Y8彩条输出
	result = camera->ops->configure_y8_colorbar(camera);
	if ( result != SYS_OK)
		return result;
	
	return SYS_OK;
}
