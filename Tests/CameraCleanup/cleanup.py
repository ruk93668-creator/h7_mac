import hashlib
import json
from pathlib import Path
import shutil
import re

here = Path(__file__).resolve().parent
root = here.parent.parent
paths = [p for d in ['CM7', 'CM4', 'Common', 'Board', 'Drivers'] for p in (root / d).rglob('*')
         if p.is_file() and p.suffix in ['.c', '.h']]
paths += list((root / 'MDK-ARM').glob('*.sct')) + [root / 'MDK-ARM/h7_mac.uvprojx']
(here / 'baseline.json').write_text(json.dumps({str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
    for p in paths}, indent=2), encoding='utf-8')

# 实板数据和既有报告作为保留基线，不参与清理。
protected = [p for p in (root / 'Tests/CameraVisualization').rglob('*') if p.is_file()]
protected += [root / 'MDK-ARM/camera_frame.hex']
protected += list((root / 'Tests').rglob('*.md'))
(here / 'protected_data.json').write_text(json.dumps({str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
    for p in protected if p.is_file()}, indent=2), encoding='utf-8')
for name in ['CM7/Application/app_camera.c', 'CM7/Application/app_camera.h',
             'CM7/Application/app_camera_check.c', 'CM7/Application/app_camera_check.h', 'MDK-ARM/h7_mac.uvprojx']:
    dest = here / 'before' / name
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(root / name, dest)

# 将旧联调入口和统计模块归档，保持独立历史验收可运行。
archive = root / 'Tests/CameraSingleFrame/diagnostics'
archive.mkdir(exist_ok=True)
for name in ['app_camera.c', 'app_camera.h', 'app_camera_check.c', 'app_camera_check.h']:
    assert not (archive / name).exists(), name
    shutil.copyfile(root / 'CM7/Application' / name, archive / name)

def write(name, text):
    (root / name).write_bytes(text.replace('\r\n', '\n').replace('\n', '\r\n').encode('gbk'))

write('CM7/Application/app_camera.c', '''#include "app_camera.h"
#include "camera_capture.h"
#include "ov5640_device.h"
#include <stddef.h>
#include <stdbool.h>

//统一选择当前分辨率和像素格式
static const CameraConfig_t s_camera_config =
{
	.width  = 160U,
	.height = 120U,
	.format = CAMERA_PIXEL_FORMAT_Y8
};

static bool s_camera_initialized;

const CameraConfig_t *App_CameraGetConfig(void)
{
	return &s_camera_config;
}

SysResult_t App_CameraInit(void)
{
	Device_t *device;
	Ov5640Device_t *camera;
	SysResult_t result;

	//初始化成功后不重复配置传感器和Media
	if ( s_camera_initialized)
		return SYS_OK;

	//获取自动注册的摄像头设备
	device = Device_Find("ov5640");
	if ( device == NULL)
		return SYS_ERR_NOT_FOUND;
	camera = FRAMEWORK_CONTAINER_OF(device, Ov5640Device_t, base);
	if ( camera->ops == NULL || camera->ops->configure == NULL)
		return SYS_ERR_NOT_SUPPORTED;

	//保留现有Y8彩条配置，成功后初始化Media
	result = camera->ops->configure(camera, App_CameraGetConfig());
	if ( result != SYS_OK)
		return result;
	result = CameraCapture_Init(App_CameraGetConfig());
	if ( result != SYS_OK)
		return result;

	s_camera_initialized = true;
	return SYS_OK;
}

void App_CameraRun(void)
{
	//只推进Media，由实际业务显式启动和消费图像
	CameraCapture_Process();
}
''')
header = root / 'CM7/Application/app_camera.h'
text = header.read_bytes().decode('gbk').replace('\r\n', '\n')
text = text.replace('//配置传感器并初始化Media，只启动一次单帧采集', '//配置传感器并初始化Media，不自动启动采集')
text = text.replace('//推进单帧采集、验收和释放，由App_Run持续调用', '//推进Media采集流程，由App_Run持续调用')
write('CM7/Application/app_camera.h', text)

project = root / 'MDK-ARM/h7_mac.uvprojx'
data = project.read_bytes()
for name in ['app_camera_check.c', 'app_camera_check.h']:
    pattern = (rb'[ \t]*<File>\s*<FileName>' + re.escape(name.encode('ascii'))
               + rb'</FileName>.*?</File>\r?\n')
    data, count = re.subn(pattern, b'', data, flags=re.S)
    assert count == 1, (name, count)
project.write_bytes(data)
for name in ['app_camera_check.c', 'app_camera_check.h']:
    path = root / 'CM7/Application' / name
    assert path.read_bytes() == (archive / name).read_bytes()
    path.unlink()

# 活跃配置回归直接测试清理后的Application，不再依赖诊断模块。
for folder in ['Tests/SdramV0/Camera', 'Tests/CameraConfig/Camera']:
    path = root / folder / 'camera_test.c'
    text = path.read_text(encoding='utf-8')
    assert '#include "../../../CM7/Application/app_camera_check.c"\n' in text
    text = text.replace('#include "../../../CM7/Application/app_camera_check.c"\n', '')
    path.write_text(text, encoding='utf-8')

# 历史单帧套件使用归档入口；底层Device/Media/BSP仍读取正式源码。
path = root / 'Tests/CameraSingleFrame/Camera/camera_test.c'
text = path.read_text(encoding='utf-8')
text = text.replace('#include "../../../CM7/Application/app_camera_check.c"', '#include "../diagnostics/app_camera_check.c"')
text = text.replace('#include "../../../CM7/Application/app_camera.c"', '#include "../diagnostics/app_camera.c"')
path.write_text(text, encoding='utf-8')
print('Formal diagnostics removed; archived test entry and real frame artifacts preserved.')
