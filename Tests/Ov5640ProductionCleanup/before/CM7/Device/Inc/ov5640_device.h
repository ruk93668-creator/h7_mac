#ifndef CM7_OV5640_DEVICE_H
#define CM7_OV5640_DEVICE_H

#include "device.h"

#define OV5640_CHIP_ID 0x5640U

typedef struct Ov5640Device Ov5640Device_t;
//保存关键寄存器回读，失败时保留出错位置
#define OV5640_VERIFY_COUNT 17U

typedef struct
{
	uint16_t chip_id;
	uint16_t width;
	uint16_t height;
	uint16_t failed_reg;
	uint8_t expected;
	uint8_t actual;
	uint8_t checked_count;
	uint8_t values[OV5640_VERIFY_COUNT];
	bool configured;
} Ov5640ConfigReport_t;

typedef struct
{
    //读取并校验芯片ID
    SysResult_t (*read_id)(
        Ov5640Device_t *camera,
        uint16_t *id
    );
	//配置160x120 Y8彩条并回读，不启动MCU采集
	SysResult_t (*configure_y8_colorbar)(
		Ov5640Device_t *camera,
		Ov5640ConfigReport_t *report
	);
} Ov5640Ops_t;

//仅提供芯片ID读取，不启用图像采集
struct Ov5640Device
{
    Device_t base;
    const Ov5640Ops_t *ops;
};

#endif
