#ifndef CM7_OV5640_DEVICE_H
#define CM7_OV5640_DEVICE_H

#include "device.h"
#include "camera_config.h"

#define OV5640_CHIP_ID 0x5640U

typedef struct Ov5640Device Ov5640Device_t;

typedef struct
{
	//读取并校验芯片ID
	SysResult_t (*read_id)(
		Ov5640Device_t *camera,
		uint16_t *id
	);

	//按公共配置设置输出，当前仅支持160x120 Y8
	SysResult_t (*configure)(
		Ov5640Device_t *camera,
		const CameraConfig_t *config
	);
} Ov5640Ops_t;

//封装传感器配置，不管理图像采集
struct Ov5640Device
{
	Device_t base;
	const Ov5640Ops_t *ops;
};

#endif
