#include "app_camera_check.h"
#include <stddef.h>

//统计仅用于单帧联调，实板通过后可独立移除
volatile CameraCheckReport_t g_camera_check;

void App_CameraCheckReset(void)
{
	g_camera_check = (CameraCheckReport_t){0};
}

void App_CameraCheckResult(CameraCheckStep_t step, SysResult_t result)
{
	switch (step)
	{
		case CAMERA_CHECK_SENSOR:  g_camera_check.sensor_result = result; break;
		case CAMERA_CHECK_MEDIA:   g_camera_check.media_result = result; break;
		case CAMERA_CHECK_START:   g_camera_check.start_result = result; break;
		case CAMERA_CHECK_CAPTURE: g_camera_check.capture_result = result; break;
		case CAMERA_CHECK_FRAME:   g_camera_check.frame_result = result; break;
		case CAMERA_CHECK_RELEASE: g_camera_check.release_result = result; break;
		case CAMERA_CHECK_STOP:    g_camera_check.stop_result = result; break;
		default: return;
	}
	g_camera_check.last_step = (uint32_t)step;
	g_camera_check.attempted_steps |= 1UL << (uint32_t)step;
}

SysResult_t App_CameraCheckFrame(
	const CameraFrame_t *frame,
	const CameraConfig_t *config
)
{
	uint32_t i;
	uint32_t bit;
	uint32_t value;
	uint32_t minimum = 255U;
	uint32_t maximum = 0U;
	uint32_t sum = 0U;
	uint32_t crc = 0xFFFFFFFFUL;
	uint32_t expected_bytes;

	//描述无效时不访问Framebuffer
	if ( frame == NULL || config == NULL || frame->data == NULL)
		return SYS_ERR_INVALID_ARG;
	if ( config->width == 0U || config->height == 0U ||
		config->format != CAMERA_PIXEL_FORMAT_Y8)
		return SYS_ERR_INVALID_ARG;
	expected_bytes = (uint32_t)config->width * config->height;
	if ( frame->width != config->width || frame->height != config->height ||
		frame->format != config->format || frame->size_bytes != expected_bytes)
		return SYS_ERR_PROTOCOL;

	g_camera_check.frame_count++;
	g_camera_check.frame_address = (uint32_t)(uintptr_t)frame->data;
	g_camera_check.width = frame->width;
	g_camera_check.height = frame->height;
	g_camera_check.format = (uint32_t)frame->format;
	g_camera_check.size_bytes = frame->size_bytes;

	//逐字节统计，CRC32采用IEEE反射算法
	for (i = 0U; i < frame->size_bytes; i++)
	{
		value = frame->data[i];
		if ( value < minimum)
			minimum = value;
		if ( value > maximum)
			maximum = value;
		sum += value;
		crc ^= value;
		for (bit = 0U; bit < 8U; bit++)
		{
			if ( (crc & 1U) != 0U)
				crc = (crc >> 1) ^ 0xEDB88320UL;
			else
				crc >>= 1;
		}
	}

	g_camera_check.sampled_bytes = frame->size_bytes;
	g_camera_check.pixel_min = minimum;
	g_camera_check.pixel_max = maximum;
	g_camera_check.pixel_sum = sum;
	g_camera_check.crc32 = crc ^ 0xFFFFFFFFUL;
	g_camera_check.stats_valid = 1U;

	//内部彩条应有亮度差，单色帧保留统计但验收失败
	return minimum < maximum ? SYS_OK : SYS_ERR_PROTOCOL;
}

void App_CameraCheckFinish(bool success)
{
	g_camera_check.success = success ? 1U : 0U;
	g_camera_check.finished = 1U;
}
