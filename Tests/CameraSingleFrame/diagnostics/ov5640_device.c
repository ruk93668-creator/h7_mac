#include "ov5640_device.h"
#include "bsp_camera.h"
#include "ov5640.h"
#include "platform_time.h"
#include <stddef.h>

//内部操作函数
static SysResult_t Ov5640_Init(Device_t *device);

static SysResult_t Ov5640_ReadId(
	Ov5640Device_t *camera,
	uint16_t *id
);

static SysResult_t Ov5640_ConfigureY8Colorbar(
	Ov5640Device_t *camera,
	const CameraConfig_t *config
);

//绑定设备生命周期回调，当前只提供初始化
static const DeviceLifecycleOps_t s_camera_lifecycle =
{
	.init  = Ov5640_Init,
	.start = NULL,
	.stop  = NULL
};

//绑定摄像头专有操作
static const Ov5640Ops_t s_camera_ops =
{
	.read_id   = Ov5640_ReadId,
	.configure = Ov5640_ConfigureY8Colorbar
};

//静态摄像头设备实例
static Ov5640Device_t s_ov5640 =
{
	.base = {
		.name      = "ov5640",
		.state     = DEVICE_STATE_UNINIT,
		.lifecycle = &s_camera_lifecycle,
		.auto_init = true
	},

	.ops = &s_camera_ops
};

//导出设备对象，由Device Model自动注册
DEVICE_EXPORT(s_ov5640);

//设备初始化只确认板级接口，不写传感器配置表
static SysResult_t Ov5640_Init(Device_t *device)
{
	if ( device == NULL)
		return SYS_ERR_INVALID_ARG;
	return BSP_Camera_Init();
}

//读取0x300A和0x300B，通信成功后检查芯片型号
static SysResult_t Ov5640_ReadId(
	Ov5640Device_t *camera,
	uint16_t *id
)
{
	uint8_t high;
	uint8_t low;
	SysResult_t result;

	//检查设备和输出指针
	if ( camera == NULL || id == NULL)
		return SYS_ERR_INVALID_ARG;

	*id = 0U;
	if ( camera->base.state != DEVICE_STATE_READY)
		return SYS_ERR_INVALID_STATE;

	//读取芯片ID高字节
	result = BSP_Camera_ReadReg(0x300AU, &high);
	if ( result != SYS_OK)
		return result;

	//读取芯片ID低字节
	result = BSP_Camera_ReadReg(0x300BU, &low);
	if ( result != SYS_OK)
		return result;

	//合并ID并检查芯片型号
	*id = (uint16_t)(((uint16_t)high << 8) | low);
	return *id == OV5640_CHIP_ID ? SYS_OK : SYS_ERR_PROTOCOL;
}

//复用旧组件，保存首次总线错误
static OV5640_Object_t s_sensor;
static SysResult_t s_bus_result;

static int32_t Ov5640_BusInit(void)
{
	s_bus_result = BSP_Camera_Init();
	return s_bus_result;
}

//组件连续寄存器访问拆成单字节SCCB事务
static int32_t Ov5640_BusTransfer(
	uint16_t address,
	uint16_t reg,
	uint8_t *data,
	uint16_t length,
	bool write
)
{
	uint16_t i;

	if ( s_bus_result != SYS_OK)
		return s_bus_result;
	if ( address != 0x78U || data == NULL || length == 0U ||
		(uint32_t)reg + length > 0x10000UL)
	{
		s_bus_result = SYS_ERR_INVALID_ARG;
		return s_bus_result;
	}

	for (i = 0U;
		 i < length;
		 i++)
	{
		if ( write)
			s_bus_result = BSP_Camera_WriteReg((uint16_t)(reg + i), data[i]);
		else
			s_bus_result = BSP_Camera_ReadReg((uint16_t)(reg + i), &data[i]);
		if ( s_bus_result != SYS_OK)
			return s_bus_result;
	}
	return SYS_OK;
}

static int32_t Ov5640_BusWrite(
	uint16_t address,
	uint16_t reg,
	uint8_t *data,
	uint16_t length
)
{
	return Ov5640_BusTransfer(address, reg, data, length, true);
}

static int32_t Ov5640_BusRead(
	uint16_t address,
	uint16_t reg,
	uint8_t *data,
	uint16_t length
)
{
	return Ov5640_BusTransfer(address, reg, data, length, false);
}

static int32_t Ov5640_GetTick(void)
{
	return (int32_t)Platform_TimeGetMs();
}

//按旧工程保留PCLK配置和行帧周期，不声明实测频率
static const struct
{
	uint16_t reg;
	uint8_t value;
} s_frame_timing[] =
{
	{0x380CU, 0x06U},
	{0x380DU, 0x0EU},
	{0x380EU, 0x03U},
	{0x380FU, 0xE8U}
};

static SysResult_t Ov5640_ConfigureY8Colorbar(
	Ov5640Device_t *camera,
	const CameraConfig_t *config
)
{
	OV5640_IO_t io = {0};
	uint16_t id;
	SysResult_t result;
	uint32_t i;

	//检查配置指针和有效尺寸
	if ( camera == NULL || config == NULL ||
		config->width == 0U || config->height == 0U)
		return SYS_ERR_INVALID_ARG;

	//仅开放已验证的配置，不将组件其他模式视为已支持
	if ( config->width != 160U || config->height != 120U ||
		config->format != CAMERA_PIXEL_FORMAT_Y8)
		return SYS_ERR_NOT_SUPPORTED;

	if ( camera->base.state == DEVICE_STATE_BUSY)
		return SYS_ERR_BUSY;
	if ( camera->base.state != DEVICE_STATE_READY)
		return SYS_ERR_INVALID_STATE;

	//先确认芯片型号，避免向其他器件写入配置
	result = Ov5640_ReadId(camera, &id);
	if ( result != SYS_OK)
		return result;
	camera->base.state = DEVICE_STATE_BUSY;

	//清除组件初始化标记，允许失败后重新写表
	s_sensor.IsInitialized = 0U;

	//选择DVP并行输出模式
	s_sensor.Mode = PARALLEL_MODE;

	//清除上次总线错误，记录本次首次失败结果
	s_bus_result = SYS_OK;

	//绑定总线初始化函数
	io.Init     = Ov5640_BusInit;

	//配置SCCB写地址，7位地址0x3C左移一位
	io.Address  = 0x78U;

	//绑定SCCB寄存器读取函数
	io.ReadReg  = Ov5640_BusRead;

	//绑定SCCB寄存器写入函数
	io.WriteReg = Ov5640_BusWrite;

	//绑定毫秒时间接口，供组件内部延时使用
	io.GetTick  = Ov5640_GetTick;

	//将总线接口绑定到OV5640组件
	if ( OV5640_RegisterBusIO(&s_sensor, &io) != OV5640_OK ||
		OV5640_Init(&s_sensor, OV5640_R160x120, OV5640_Y8) != OV5640_OK ||
		OV5640_SetPCLK(&s_sensor, OV5640_PCLK_48M) != OV5640_OK)
		goto component_error;

	for (i = 0U;
		 i < sizeof(s_frame_timing) / sizeof(s_frame_timing[0]);
		 i++)
	{
		result = BSP_Camera_WriteReg(s_frame_timing[i].reg, s_frame_timing[i].value);
		if ( result != SYS_OK)
			goto done;
	}

	//这里只开启传感器输出，不启动DCMI、DMA或SDRAM
	if ( OV5640_ColorbarModeConfig(&s_sensor, COLORBAR_MODE_ENABLE) != OV5640_OK ||
		OV5640_Start(&s_sensor) != OV5640_OK)
		goto component_error;

	result = SYS_OK;
	goto done;
component_error:
	//组件的-1不能直接当作通用参数错误
	result = s_bus_result != SYS_OK ? s_bus_result : SYS_ERR_IO;
done:
	camera->base.state = DEVICE_STATE_READY;
	return result;
}
