from pathlib import Path
import hashlib,json,shutil
r=Path('.');t=r/'Tests/Ov5640Y8';
paths=list((r/'CM4').rglob('*'))+[r/'CM7/Core/Src/main.c',r/'CM7/Framework/framework_runtime.c']+list((r/'MDK-ARM').glob('*.sct'))
(t/'baseline.json').write_text(json.dumps({str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths if p.is_file()},indent=2),encoding='utf-8')
def read(p):return Path(p).read_bytes().decode('gbk')
def write(p,s):Path(p).write_bytes(s.replace('\r\n','\n').replace('\n','\r\n').encode('gbk'))
comp=r/'CM7/Device/Components/OV5640';comp.mkdir(parents=True,exist_ok=True)
for f in ['ov5640.c','ov5640.h','ov5640_reg.c','ov5640_reg.h']:shutil.copyfile(t/'reference'/f,comp/f)
shutil.copyfile(t/'reference/ov5640_LICENSE.md',comp/'LICENSE')
p=r/'CM7/BSP/bsp_camera.h';s=read(p);s=s.replace('\n#endif','\n//通过软件SCCB写入一个寄存器\nSysResult_t BSP_Camera_WriteReg(\n\tuint16_t reg,\n\tuint8_t value\n);\n\n#endif');write(p,s)
p=r/'CM7/BSP/bsp_camera.c';s=read(p);pos=s.index('//注册板级初始化');s=s[:pos]+'''//发送写地址、寄存器地址和数据，每个字节检查ACK
SysResult_t BSP_Camera_WriteReg(
	uint16_t reg,
	uint8_t value
)
{
	uint32_t error = 0U;

	if ( !s_camera_ready)
		return SYS_ERR_INVALID_STATE;

	//总线忙时不开始事务
	Camera_Sda(1U);
	Camera_Scl(1U);
	Camera_DelayUs(10U);
	if ( HAL_GPIO_ReadPin(GPIOB, CAMERA_SDA) == GPIO_PIN_RESET)
		error |= 0x01U;
	if ( HAL_GPIO_ReadPin(GPIOB, CAMERA_SCL) == GPIO_PIN_RESET)
		error |= 0x02U;
	if ( error != 0U)
	{
		s_sccb_error = error;
		return SYS_ERR_BUSY;
	}

	Camera_Start();
	if ( !Camera_WriteByte(0x78U))
	{
		error = 0x04U;
		goto done;
	}
	if ( !Camera_WriteByte((uint8_t)(reg >> 8)))
	{
		error = 0x08U;
		goto done;
	}
	if ( !Camera_WriteByte((uint8_t)reg))
	{
		error = 0x10U;
		goto done;
	}
	if ( !Camera_WriteByte(value))
		error = 0x40U;
done:
	//失败也释放总线，软件复位后等待寄存器恢复
	Camera_Stop();
	s_sccb_error = error;
	if ( error != 0U)
		return SYS_ERR_IO;
	if ( reg == 0x3008U && value == 0x82U)
		HAL_Delay(5U);
	return SYS_OK;
}

'''+s[pos:];write(p,s)
p=r/'CM7/Device/Inc/ov5640_device.h';s=read(p);s=s.replace('typedef struct\n{\n    //读取', '''//保存关键寄存器回读，失败时保留出错位置
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
    //读取''');s=s.replace('    );\n} Ov5640Ops_t;', '''    );
	//配置160x120 Y8彩条并回读，不启动MCU采集
	SysResult_t (*configure_y8_colorbar)(
		Ov5640Device_t *camera,
		Ov5640ConfigReport_t *report
	);
} Ov5640Ops_t;''');s=s.replace('//只提供芯片ID读取，不启动图像采集','//通过Ops配置传感器，不管理DCMI和图像缓存');write(p,s)
p=r/'CM7/Device/Src/ov5640_device.c';s=read(p);s=s.replace('#include <stddef.h>','#include "ov5640.h"\n#include "platform_time.h"\n#include <stddef.h>');start=s.index('//绑定设备生命周期回调')
addition='''//复用旧组件，保存首次总线错误
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

//回读尺寸、格式、彩条、运行状态、时序和DVP输出
static const struct
{
	uint16_t reg;
	uint8_t value;
} s_verify[OV5640_VERIFY_COUNT] =
{
	{0x3808U, 0x00U}, {0x3809U, 0xA0U},
	{0x380AU, 0x00U}, {0x380BU, 0x78U},
	{0x4300U, 0x10U}, {0x501FU, 0x00U},
	{0x503DU, 0x80U}, {0x5584U, 0x40U},
	{0x3008U, 0x02U},
	{0x380CU, 0x06U}, {0x380DU, 0x0EU},
	{0x380EU, 0x03U}, {0x380FU, 0xE8U},
	{0x3035U, 0x11U}, {0x3036U, 0x60U},
	{0x3017U, 0xFFU}, {0x3018U, 0xFFU}
};

static SysResult_t Ov5640_ConfigureY8Colorbar(
	Ov5640Device_t *camera,
	Ov5640ConfigReport_t *report
)
{
	OV5640_IO_t io = {0};
	Ov5640ConfigReport_t empty = {0};
	SysResult_t result;
	uint32_t i;

	if ( camera == NULL || report == NULL)
		return SYS_ERR_INVALID_ARG;
	*report = empty;
	if ( camera->base.state == DEVICE_STATE_BUSY)
		return SYS_ERR_BUSY;
	if ( camera->base.state != DEVICE_STATE_READY)
		return SYS_ERR_INVALID_STATE;

	//先确认芯片型号，避免向其他器件写入配置
	result = Ov5640_ReadId(camera, &report->chip_id);
	if ( result != SYS_OK)
		return result;
	camera->base.state = DEVICE_STATE_BUSY;

	//每次显式配置重新写表，允许失败后重试
	s_sensor.IsInitialized = 0U;
	s_sensor.Mode = PARALLEL_MODE;
	s_bus_result = SYS_OK;
	io.Init = Ov5640_BusInit;
	io.Address = 0x78U;
	io.ReadReg = Ov5640_BusRead;
	io.WriteReg = Ov5640_BusWrite;
	io.GetTick = Ov5640_GetTick;
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

	for (i = 0U;
		 i < OV5640_VERIFY_COUNT;
		 i++)
	{
		report->failed_reg = s_verify[i].reg;
		report->expected = s_verify[i].value;
		result = BSP_Camera_ReadReg(s_verify[i].reg, &report->values[i]);
		if ( result != SYS_OK)
			goto done;
		report->actual = report->values[i];
		if ( report->actual != report->expected)
		{
			result = SYS_ERR_PROTOCOL;
			goto done;
		}
		report->checked_count++;
	}

	report->width = (uint16_t)((report->values[0] << 8) | report->values[1]);
	report->height = (uint16_t)((report->values[2] << 8) | report->values[3]);
	report->failed_reg = 0U;
	report->configured = true;
	result = SYS_OK;
	goto done;
component_error:
	//组件的-1不能直接当作通用参数错误
	result = s_bus_result != SYS_OK ? s_bus_result : SYS_ERR_IO;
done:
	camera->base.state = DEVICE_STATE_READY;
	return result;
}

'''
s=s[:start]+addition+s[start:];s=s.replace('\tOv5640_ReadId\n','\tOv5640_ReadId,\n\tOv5640_ConfigureY8Colorbar\n');write(p,s)
write(r/'CM7/Application/app_camera.h','''#ifndef CM7_APP_CAMERA_H
#define CM7_APP_CAMERA_H

#include "ov5640_device.h"

//保存一次启动配置结果，供调试器查看
typedef struct
{
	SysResult_t result;
	bool done;
	Ov5640ConfigReport_t sensor;
} AppCameraReport_t;

extern volatile AppCameraReport_t g_camera_report;

SysResult_t App_CameraInit(void);

#endif
''')
write(r/'CM7/Application/app_camera.c','''#include "app_camera.h"
#include <stddef.h>

volatile AppCameraReport_t g_camera_report;

//仅启动一次配置和回读，不创建测试Timer
SysResult_t App_CameraInit(void)
{
	Device_t *device;
	Ov5640Device_t *camera;
	Ov5640ConfigReport_t report = {0};
	SysResult_t result;

	if ( g_camera_report.done && g_camera_report.result == SYS_OK)
		return SYS_OK;

	device = Device_Find("ov5640");
	if ( device == NULL)
		result = SYS_ERR_NOT_FOUND;
	else
	{
		//base是设备对象首成员
		camera = (Ov5640Device_t *)device;
		if ( camera->ops == NULL || camera->ops->configure_y8_colorbar == NULL)
			result = SYS_ERR_NOT_SUPPORTED;
		else
			result = camera->ops->configure_y8_colorbar(camera, &report);
	}

	g_camera_report.sensor = report;
	g_camera_report.result = result;
	g_camera_report.done = true;
	return result;
}
''')
p=r/'CM7/Application/app_main.c';s=read(p).replace('#include "app_main.h"','#include "app_main.h"\n#include "app_camera.h"').replace('\treturn SYS_OK;','\treturn App_CameraInit();');write(p,s)
p=r/'MDK-ARM/h7_mac.uvprojx';s=p.read_bytes().decode('utf-8')
# Add files to existing CM7 groups; CM4 excludes these entire groups.
for anchor,files in [('ov5640_device.h',[('ov5640.c','Device/Components/OV5640'),('ov5640_reg.c','Device/Components/OV5640')]),('app_main.h',[('app_camera.c','Application'),('app_camera.h','Application')])]:
 import re
 pattern=r'(<File>\s*<FileName>'+re.escape(anchor)+r'</FileName>.*?<FilePath>../CM7/.*?</FilePath>\s*</File>)'
 block=''
 for f,d in files:block+='\n            <File>\n              <FileName>'+f+'</FileName>\n              <FileType>'+('1' if f.endswith('.c') else '5')+'</FileType>\n              <FilePath>../CM7/'+d+'/'+f+'</FilePath>\n            </File>'
 s,n=re.subn(pattern,lambda m:m.group(1)+block,s,flags=re.S);assert n==2,(anchor,n)
s=s.replace('..\\CM7\\Framework</IncludePath>','..\\CM7\\Framework;..\\CM7\\Device\\Components\\OV5640</IncludePath>')
p.write_bytes(s.encode('utf-8'))
