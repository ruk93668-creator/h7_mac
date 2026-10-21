from pathlib import Path
import hashlib,json,shutil,re
r=Path('.');t=r/'Tests/Ov5640ProductionCleanup';t.mkdir(exist_ok=True)
# Save production baseline and originals for review.
changed=['CM7/Application/app_camera.c','CM7/Application/app_camera.h','CM7/Device/Inc/ov5640_device.h','CM7/Device/Src/ov5640_device.c']
paths=[p for d in ['CM7','CM4','Common','Board'] for p in (r/d).rglob('*') if p.is_file()]+list((r/'MDK-ARM').glob('*.sct'))+[r/'MDK-ARM/h7_mac.uvprojx']
(t/'baseline.json').write_text(json.dumps({str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},indent=2),encoding='utf-8')
for f in changed:
 dst=t/'before'/f;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(f,dst)
def read(f):return Path(f).read_bytes().decode('gbk').replace('\r\r\n','\n').replace('\r\n','\n')
def write(f,s):Path(f).write_bytes(s.replace('\n','\r\n').encode('gbk'))
f='CM7/Device/Src/ov5640_device.c';s=read(f)
a=s.index('//回读尺寸、格式');b=s.index('static SysResult_t Ov5640_ConfigureY8Colorbar(',a)
verify=s[a:b]
(t/'register_verify_table.txt').write_text(verify,encoding='utf-8')
s=s[:a]+s[b:]
s=s.replace('Ov5640Device_t *camera,\n\tOv5640ConfigReport_t *report','Ov5640Device_t *camera')
s=s.replace('\tOv5640ConfigReport_t empty = {0};','\tuint16_t id;')
s=s.replace('if ( camera == NULL || report == NULL)','if ( camera == NULL)').replace('\t*report = empty;\n','')
s=s.replace('Ov5640_ReadId(camera, &report->chip_id)','Ov5640_ReadId(camera, &id)')
a=s.index('\tfor (i = 0U;',s.index('OV5640_Start(&s_sensor)'));b=s.index('\tresult = SYS_OK;',a)
s=s[:a]+s[b:]
write(f,s)
write('CM7/Device/Inc/ov5640_device.h','''#ifndef CM7_OV5640_DEVICE_H
#define CM7_OV5640_DEVICE_H

#include "device.h"

#define OV5640_CHIP_ID 0x5640U

typedef struct Ov5640Device Ov5640Device_t;

typedef struct
{
	//读取并校验芯片ID
	SysResult_t (*read_id)(
		Ov5640Device_t *camera,
		uint16_t *id
	);

	//配置160x120 Y8彩条输出
	SysResult_t (*configure_y8_colorbar)(
		Ov5640Device_t *camera
	);
} Ov5640Ops_t;

//封装传感器配置，不管理图像采集
struct Ov5640Device
{
	Device_t base;
	const Ov5640Ops_t *ops;
};

#endif
''')
write('CM7/Application/app_camera.h','''#ifndef CM7_APP_CAMERA_H
#define CM7_APP_CAMERA_H

#include "sys_result.h"

//启动摄像头配置
SysResult_t App_CameraInit(void);

#endif
''')
write('CM7/Application/app_camera.c','''#include "app_camera.h"
#include "ov5640_device.h"
#include <stddef.h>

//避免重复初始化成功的应用模块
static bool s_camera_initialized;

SysResult_t App_CameraInit(void)
{
	Device_t *device;
	Ov5640Device_t *camera;
	SysResult_t result;

	if ( s_camera_initialized)
		return SYS_OK;

	//获取自动注册的摄像头设备
	device = Device_Find("ov5640");
	if ( device == NULL)
		return SYS_ERR_NOT_FOUND;

	camera = FRAMEWORK_CONTAINER_OF(device, Ov5640Device_t, base);
	if ( camera->ops == NULL || camera->ops->configure_y8_colorbar == NULL)
		return SYS_ERR_NOT_SUPPORTED;

	//检查芯片ID并配置Y8彩条输出
	result = camera->ops->configure_y8_colorbar(camera);
	if ( result != SYS_OK)
		return result;

	s_camera_initialized = true;
	return SYS_OK;
}
''')
print('Updated four production files; saved baseline and 17-register table in independent test directory.')
