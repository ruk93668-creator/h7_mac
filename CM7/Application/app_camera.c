#include "app_camera.h"
#include "video_config.h"
#include "camera_capture.h"
#include "ov5640_device.h"
#include <stddef.h>
#include <stdbool.h>

//相机APP保存已接受配置，底层不读取业务默认值
static CameraConfig_t s_camera_config;
static bool s_camera_config_set;

//分别记录传感器配置和公共初始化结果，失败重试不重复已成功步骤
static bool s_camera_configured;
static bool s_camera_initialized;

//仅通过Device生命周期控制传感器输出
static Device_t *s_camera_device;

//公共初始化，只配置OV5640和Media，不启动采集
static SysResult_t App_CameraPrepare(void);

//先启动传感器，再沿用Media的采集入口
static SysResult_t App_CameraStart(bool stream);

SysResult_t App_CameraConfigure(const CameraConfig_t *config)
{
    SysResult_t result;

    if (s_camera_config_set || s_camera_configured || s_camera_initialized)
        return SYS_ERR_INVALID_STATE;

    result = Ov5640_ValidateConfig(config);
    if (result != SYS_OK)
        return result;

    //JPEG只交给Snapshot接收，不能用固定长度Stream校验推算容量
    result = CameraConfig_IsVariableLength(config->format) ?
        SYS_OK : CameraCapture_ValidateStreamConfig(config);
    if (result != SYS_OK)
        return result;

    s_camera_config = *config;
    s_camera_config_set = true;
    return SYS_OK;
}

const CameraConfig_t *App_CameraGetConfig(void)
{
	return s_camera_config_set ? &s_camera_config : &VideoConfig_GetDefault()->camera;
}

SysResult_t App_CameraInit(void)
{
	return App_CameraStart(false);
}

SysResult_t App_CameraInitStream(void)
{
	return App_CameraStart(true);
}

SysResult_t App_CameraStop(void)
{
	SysResult_t result;

	if ( !s_camera_initialized || s_camera_device == NULL)
		return SYS_ERR_INVALID_STATE;

	//先确认DCMI和DMA安全停止，失败时保留传感器状态
	result = CameraCapture_Stop();
	if ( result != SYS_OK)
		return result;

	//故障停止可能进入DRAINING，必须等待消费者归还帧
	if ( !CameraCapture_IsStopped())
		return SYS_ERR_BUSY;

	//已停止的传感器不重复提交生命周期操作
	if ( s_camera_device->state == DEVICE_STATE_READY)
		return SYS_OK;

	return Device_Stop(s_camera_device);
}

void App_CameraRun(void)
{
	//推进采集流程，完整图像交给业务获取和释放
	CameraCapture_Process();

	//Media完成故障恢复或业务释放后，再停止传感器输出
	if ( s_camera_initialized && CameraCapture_IsStopped() &&
		s_camera_device != NULL && s_camera_device->state == DEVICE_STATE_RUNNING)
		(void)App_CameraStop();
}

static SysResult_t App_CameraPrepare(void)
{
	Device_t *device;
	Ov5640Device_t *camera;
	SysResult_t result;

    //相机APP自行读取默认配置，向Device和Media显式传递
    if (!s_camera_config_set)
    {
        result = App_CameraConfigure(&VideoConfig_GetDefault()->camera);
        if (result != SYS_OK)
            return result;
    }

	//公共初始化成功后，不再重复配置传感器和Media
	if ( s_camera_initialized)
		return SYS_OK;

	if ( !s_camera_configured)
	{
		//获取自动注册的摄像头设备
		device = Device_Find("ov5640");
		if ( device == NULL)
			return SYS_ERR_NOT_FOUND;
		s_camera_device = device;
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

static SysResult_t App_CameraStart(bool stream)
{
	SysResult_t result;
	SysResult_t stop_result;
	bool sensor_started = false;

	result = App_CameraPrepare();
	if ( result != SYS_OK)
		return result;

	//重复采集由Media拒绝，不重复启动已经运行的传感器
	if ( s_camera_device->state == DEVICE_STATE_READY)
	{
		result = Device_Start(s_camera_device);
		if ( result != SYS_OK)
			return result;
		sensor_started = true;
	}
	else if ( s_camera_device->state == DEVICE_STATE_BUSY)
		return SYS_ERR_BUSY;
	else if ( s_camera_device->state != DEVICE_STATE_RUNNING)
		return SYS_ERR_INVALID_STATE;

	result = stream ? CameraCapture_StartStream() : CameraCapture_Start();
	if ( result == SYS_OK || !sensor_started)
		return result;

	//本轮采集启动失败，先恢复Media，再停止本轮开启的传感器
	stop_result = App_CameraStop();
	return stop_result != SYS_OK ? stop_result : result;
}
