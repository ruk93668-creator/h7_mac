#include "app_camera.h"
#include "app_camera_check.h"
#include "camera_capture.h"
#include "ov5640_device.h"
#include <stddef.h>
#include <stdbool.h>

//统一选择当前分辨率和像素格式
static const CameraConfig_t s_camera_config =
{
	.width  = 160U,
	.height = 120U,
	.format = CAMERA_PIXEL_FORMAT_Y8
};

static bool s_camera_initialized;
static bool s_camera_running;
static SysResult_t s_camera_init_result;

const CameraConfig_t *App_CameraGetConfig(void)
{
	return &s_camera_config;
}

SysResult_t App_CameraInit(void)
{
	Device_t *device;
	Ov5640Device_t *camera;
	SysResult_t result;

	//同一次上电只启动一帧，重复初始化不重写传感器
	if ( s_camera_initialized)
		return s_camera_init_result;
	App_CameraCheckReset();

	//获取自动注册的摄像头设备
	device = Device_Find("ov5640");
	if ( device == NULL)
		result = SYS_ERR_NOT_FOUND;
	else
	{
		camera = FRAMEWORK_CONTAINER_OF(device, Ov5640Device_t, base);
		if ( camera->ops == NULL || camera->ops->configure == NULL)
			result = SYS_ERR_NOT_SUPPORTED;
		else
			result = camera->ops->configure(camera, App_CameraGetConfig());
	}
	App_CameraCheckResult(CAMERA_CHECK_SENSOR, result);
	if ( result != SYS_OK)
		goto init_failed;

	//传感器配置成功后，再初始化Media
	result = CameraCapture_Init(App_CameraGetConfig());
	App_CameraCheckResult(CAMERA_CHECK_MEDIA, result);
	if ( result != SYS_OK)
		goto init_failed;
	s_camera_initialized = true;

	//只提交一次采集，图像由主循环异步处理
	result = CameraCapture_Start();
	s_camera_init_result = result;
	App_CameraCheckResult(CAMERA_CHECK_START, result);
	if ( result != SYS_OK)
	{
		//启动失败也可能遗留硬件状态，单独记录恢复结果
		App_CameraCheckResult(CAMERA_CHECK_STOP, CameraCapture_Stop());
		goto init_failed;
	}
	s_camera_running = true;
	return SYS_OK;

init_failed:
	App_CameraCheckFinish(false);
	return result;
}

void App_CameraRun(void)
{
	CameraFrame_t frame;
	SysResult_t result;
	SysResult_t frame_result;
	SysResult_t release_result;

	if ( !s_camera_running)
		return;

	//持续推进Media，不等待中断或轮询硬件完成位
	CameraCapture_Process();
	result = CameraCapture_GetLastResult();
	if ( result != SYS_OK)
	{
		//Media已尝试停止，保留采集与恢复两个结果
		App_CameraCheckResult(CAMERA_CHECK_CAPTURE, result);
		s_camera_running = false;
		App_CameraCheckFinish(false);
		return;
	}

	if ( !CameraCapture_GetFrame(&frame))
		return;
	App_CameraCheckResult(CAMERA_CHECK_CAPTURE, SYS_OK);

	//单帧联调统计，统计完成前保留帧所有权
	frame_result = App_CameraCheckFrame(&frame, App_CameraGetConfig());
	App_CameraCheckResult(CAMERA_CHECK_FRAME, frame_result);
	release_result = CameraCapture_ReleaseFrame();
	App_CameraCheckResult(CAMERA_CHECK_RELEASE, release_result);

	//不自动启动下一帧，也不重复统计
	s_camera_running = false;
	App_CameraCheckFinish(frame_result == SYS_OK && release_result == SYS_OK);
}
