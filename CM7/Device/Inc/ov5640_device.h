#ifndef CM7_OV5640_DEVICE_H
#define CM7_OV5640_DEVICE_H

#include "device.h"
#include "camera_config.h"

//只校验当前驱动能力，不访问传感器
SysResult_t Ov5640_ValidateConfig(const CameraConfig_t *config);

#define OV5640_CHIP_ID 0x5640U

typedef struct Ov5640Device Ov5640Device_t;

typedef struct
{
	//读取并校验芯片ID
	SysResult_t (*read_id)(
		Ov5640Device_t *camera,
		uint16_t *id
	);

	//配置160x120并保持待机，成功后通过Device_Start开启输出
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
