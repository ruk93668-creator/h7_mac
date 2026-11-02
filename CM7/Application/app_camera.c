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

//分别记录传感器配置和公共初始化结果，失败重试不重复已成功步骤
static bool s_camera_configured;
static bool s_camera_initialized;

//公共初始化，只配置OV5640和Media，不启动采集
static SysResult_t App_CameraPrepare(void);

const CameraConfig_t *App_CameraGetConfig(void)
{
	return &s_camera_config;
}

SysResult_t App_CameraInit(void)
{
	SysResult_t result;

	result = App_CameraPrepare();
	if ( result != SYS_OK)
		return result;

	//由Media拒绝重复启动、交叉启动和未恢复故障
	return CameraCapture_Start();
}

SysResult_t App_CameraInitStream(void)
{
	SysResult_t result;

	result = App_CameraPrepare();
	if ( result != SYS_OK)
		return result;

	//沿用同一份公共配置，由Media管理连续采集状态
	return CameraCapture_StartStream();
}

void App_CameraRun(void)
{
	//推进采集流程，完整图像交给业务获取和释放
	CameraCapture_Process();
}

static SysResult_t App_CameraPrepare(void)
{
	Device_t *device;
	Ov5640Device_t *camera;
	SysResult_t result;

	//公共初始化成功后，不再重复配置传感器和Media
	if ( s_camera_initialized)
		return SYS_OK;

	if ( !s_camera_configured)
	{
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
		s_camera_configured = true;
	}

	//Media初始化失败时，保留已完成的传感器配置供下次重试
	result = CameraCapture_Init(App_CameraGetConfig());
	if ( result != SYS_OK)
		return result;

	s_camera_initialized = true;
	return SYS_OK;
}
