import shutil
from pathlib import Path

here = Path(__file__).resolve().parent
root = here.parent.parent
shutil.copyfile(here / 'Camera/results.json', here / 'baseline_camera_results.json')
shutil.copyfile(here / 'Camera/commands.json', here / 'baseline_camera_commands.json')

def write(relative, text):
    (root / relative).write_bytes(text.replace('\r\n', '\n').replace('\n', '\r\n').encode('gbk'))

def read(relative):
    return (root / relative).read_bytes().decode('gbk').replace('\r\n', '\n')

write('Common/Inc/camera_config.h', '''#ifndef CAMERA_CONFIG_H
#define CAMERA_CONFIG_H

#include <stdint.h>

//当前只支持8位灰度输出
typedef enum
{
	CAMERA_PIXEL_FORMAT_Y8 = 0U
} CameraPixelFormat_t;

//摄像头输出配置，由Application统一选择
typedef struct
{
	uint16_t width;
	uint16_t height;
	CameraPixelFormat_t format;
} CameraConfig_t;

#endif
''')

text = read('CM7/Application/app_camera.h')
text = text.replace('#include "sys_result.h"', '#include "sys_result.h"\n#include "camera_config.h"')
text = text.replace('//启动摄像头配置', '//获取当前只读配置，供Device和后续Media共用\nconst CameraConfig_t *App_CameraGetConfig(void);\n\n//启动摄像头配置')
write('CM7/Application/app_camera.h', text)

text = read('CM7/Application/app_camera.c')
text = text.replace('SysResult_t App_CameraInit(void)', '''//统一选择当前分辨率和像素格式
static const CameraConfig_t s_camera_config =
{
	.width  = 160U,
	.height = 120U,
	.format = CAMERA_PIXEL_FORMAT_Y8
};

const CameraConfig_t *App_CameraGetConfig(void)
{
	return &s_camera_config;
}

SysResult_t App_CameraInit(void)''', 1)
text = text.replace('configure_y8_colorbar == NULL', 'configure == NULL')
text = text.replace('//检查芯片ID并配置Y8彩条输出', '//将同一份配置传给Device，保留现有彩条输出')
text = text.replace('camera->ops->configure_y8_colorbar(camera)', 'camera->ops->configure(camera, App_CameraGetConfig())')
write('CM7/Application/app_camera.c', text)

text = read('CM7/Device/Inc/ov5640_device.h')
text = text.replace('#include "device.h"', '#include "device.h"\n#include "camera_config.h"')
text = text.replace('//配置160x120 Y8彩条输出\n\tSysResult_t (*configure_y8_colorbar)(\n\t\tOv5640Device_t *camera\n',
                    '//按公共配置设置输出，当前仅支持160x120 Y8彩条\n\tSysResult_t (*configure)(\n\t\tOv5640Device_t *camera,\n\t\tconst CameraConfig_t *config\n')
assert 'configure_y8_colorbar' not in text
write('CM7/Device/Inc/ov5640_device.h', text)

text = read('CM7/Device/Src/ov5640_device.c')
old = 'static SysResult_t Ov5640_ConfigureY8Colorbar(\n\tOv5640Device_t *camera\n)'
assert text.count(old) == 2
text = text.replace(old, 'static SysResult_t Ov5640_ConfigureY8Colorbar(\n\tOv5640Device_t *camera,\n\tconst CameraConfig_t *config\n)')
text = text.replace('.read_id               =', '.read_id   =').replace('.configure_y8_colorbar =', '.configure =')
marker = '\tif ( camera == NULL)\n\t\treturn SYS_ERR_INVALID_ARG;\n\tif ( camera->base.state == DEVICE_STATE_BUSY)'
replacement = '''	//检查配置指针和有效尺寸
	if ( camera == NULL || config == NULL ||
		config->width == 0U || config->height == 0U)
		return SYS_ERR_INVALID_ARG;

	//仅开放已验证的配置，不将组件其他模式视为已支持
	if ( config->width != 160U || config->height != 120U ||
		config->format != CAMERA_PIXEL_FORMAT_Y8)
		return SYS_ERR_NOT_SUPPORTED;

	if ( camera->base.state == DEVICE_STATE_BUSY)'''
assert marker in text
text = text.replace(marker, replacement, 1)
write('CM7/Device/Src/ov5640_device.c', text)

# 只登记公共头文件，保留工程原有文本和Include Path
project = root / 'MDK-ARM/h7_mac.uvprojx'
data = project.read_bytes()
newline = b'\r\n' if b'\r\n' in data else b'\n'
marker = b'            <File>' + newline + b'              <FileName>sys_result.h</FileName>'
addition = b'''            <File>\r
              <FileName>camera_config.h</FileName>\r
              <FileType>5</FileType>\r
              <FilePath>..\\Common\\Inc\\camera_config.h</FilePath>\r
            </File>\r
'''
addition = addition.replace(b'\r\n', newline)
assert data.count(marker) == 2
project.write_bytes(data.replace(marker, addition + marker))
print('Common type added; Application owns configuration; Device accepts and validates config.')
