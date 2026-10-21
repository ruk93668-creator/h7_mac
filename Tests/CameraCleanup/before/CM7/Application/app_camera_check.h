#ifndef CM7_APP_CAMERA_CHECK_H
#define CM7_APP_CAMERA_CHECK_H

#include "camera_config.h"
#include "sys_result.h"
#include <stdbool.h>

//单帧联调步骤，attempted_steps对应各步骤的位
typedef enum
{
	CAMERA_CHECK_SENSOR = 0,
	CAMERA_CHECK_MEDIA,
	CAMERA_CHECK_START,
	CAMERA_CHECK_CAPTURE,
	CAMERA_CHECK_FRAME,
	CAMERA_CHECK_RELEASE,
	CAMERA_CHECK_STOP
} CameraCheckStep_t;

//临时实板验收记录，采集错误与停止恢复结果分开保存
typedef struct
{
	uint32_t attempted_steps;
	uint32_t last_step;
	uint32_t finished;
	uint32_t success;
	SysResult_t sensor_result;
	SysResult_t media_result;
	SysResult_t start_result;
	SysResult_t capture_result;
	SysResult_t frame_result;
	SysResult_t release_result;
	SysResult_t stop_result;
	uint32_t frame_count;
	uint32_t frame_address;
	uint32_t width;
	uint32_t height;
	uint32_t format;
	uint32_t size_bytes;
	uint32_t sampled_bytes;
	uint32_t pixel_min;
	uint32_t pixel_max;
	uint32_t pixel_sum;
	uint32_t crc32;
	uint32_t stats_valid;
} CameraCheckReport_t;

//Keil Watch使用的单帧验收记录
extern volatile CameraCheckReport_t g_camera_check;

//清除上一次联调记录
void App_CameraCheckReset(void);

//记录已执行步骤和对应结果
void App_CameraCheckResult(CameraCheckStep_t step, SysResult_t result);

//校验图像描述并统计像素和CRC32，不修改图像
SysResult_t App_CameraCheckFrame(
	const CameraFrame_t *frame,
	const CameraConfig_t *config
);

//结束单帧验收，不触发下一次采集
void App_CameraCheckFinish(bool success);

#endif
