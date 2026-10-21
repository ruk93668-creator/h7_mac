#include "app_camera.h"
#include <stddef.h>

volatile AppCameraReport_t g_camera_report;

//仅启动一次配置和回读，不创建测试Timer
SysResult_t App_CameraInit(void)
{
	Device_t *device;
	Ov5640Device_t *camera;
	Ov5640ConfigReport_t report = {0};
	SysResult_t result;

	if ( g_camera_report.done && g_camera_report.result == SYS_OK)
		return SYS_OK;

	device = Device_Find("ov5640");
	if ( device == NULL)
		result = SYS_ERR_NOT_FOUND;
	else
	{
		//base是设备对象首成员
		camera = (Ov5640Device_t *)device;
		if ( camera->ops == NULL || camera->ops->configure_y8_colorbar == NULL)
			result = SYS_ERR_NOT_SUPPORTED;
		else
			result = camera->ops->configure_y8_colorbar(camera, &report);
	}

	g_camera_report.sensor = report;
	g_camera_report.result = result;
	g_camera_report.done = true;
	return result;
}
