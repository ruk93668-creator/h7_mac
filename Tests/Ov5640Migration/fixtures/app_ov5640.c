#include "app_ov5640.h"
#include "ov5640_device.h"
#include "framework_timer.h"
#include <stddef.h>

#define APP_OV5640_PERIOD_MS 1000U

volatile Ov5640ProbeReport_t g_ov5640_probe;
static Ov5640Device_t *s_probe_camera;
static FrameworkTimerHandle_t s_probe_timer = FRAMEWORK_TIMER_INVALID_HANDLE;
static bool s_probe_due;

//Timer只标记到期，SCCB读取在Application轮询中执行
static void Probe_Due(void *context)
{
	(void)context;
	s_probe_due = true;
}

//获取摄像头设备，创建并启动周期Timer
SysResult_t App_Ov5640Init(void)
{
	Device_t *device;
	Ov5640Device_t *camera;
	FrameworkTimerConfig_t config =
	{
		APP_OV5640_PERIOD_MS,
		FRAMEWORK_TIMER_PERIODIC,
		Probe_Due,
		NULL
	};
	SysResult_t result;

	//防止重复创建Timer
	if ( s_probe_camera != NULL)
		return SYS_OK;

	//查找已完成初始化的摄像头
	device = Device_Find("ov5640");
	if ( device == NULL)
		return SYS_ERR_NOT_FOUND;
	if ( device->state != DEVICE_STATE_READY)
		return SYS_ERR_INVALID_STATE;

	camera = FRAMEWORK_CONTAINER_OF(device, Ov5640Device_t, base);
	if ( camera->ops == NULL || camera->ops->read_id == NULL)
		return SYS_ERR_NOT_SUPPORTED;

	//创建周期Timer
	result = Framework_TimerCreate(&config, &s_probe_timer);
	if ( result != SYS_OK)
		return result;

	//启动Timer，失败时释放资源
	result = Framework_TimerStart(s_probe_timer);
	if ( result != SYS_OK)
	{
		(void)Framework_TimerDelete(s_probe_timer);
		s_probe_timer = FRAMEWORK_TIMER_INVALID_HANDLE;
		return result;
	}

	s_probe_camera = camera;
	return SYS_OK;
}

//每秒读取一次，成功或失败都记入结果，累计10次后停止Timer
void App_Ov5640Run(void)
{
	uint16_t id = 0U;
	SysResult_t result;
	uint32_t index;

	//没有到期任务或已经完成测试时直接返回
	if ( s_probe_camera == NULL || !s_probe_due || g_ov5640_probe.done != 0U)
		return;

	s_probe_due = false;
	index = g_ov5640_probe.count;
	if ( index >= APP_OV5640_TEST_COUNT)
		return;

	//读取ID并保存本次结果
	result = s_probe_camera->ops->read_id(s_probe_camera, &id);
	g_ov5640_probe.samples[index].chip_id = id;
	g_ov5640_probe.samples[index].result = result;

	//累计成功和失败次数
	if ( result == SYS_OK)
		g_ov5640_probe.passed++;
	else
		g_ov5640_probe.failed++;
	g_ov5640_probe.count = index + 1U;

	//达到测试次数后停止Timer
	if ( g_ov5640_probe.count == APP_OV5640_TEST_COUNT)
	{
		(void)Framework_TimerStop(s_probe_timer);
		g_ov5640_probe.done = 1U;
	}
}
