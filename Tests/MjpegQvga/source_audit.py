"""QVGA source checks only: no C compiler, executable tests or hardware."""
from pathlib import Path
import re,json,subprocess
from pycparser import c_parser,c_ast
ROOT=Path(__file__).resolve().parents[2]
rows=[]
def text(path):
 b=(ROOT/path).read_bytes()
 try:s=b.decode('utf-8')
 except UnicodeDecodeError:s=b.decode('gbk')
 return re.sub(r'\r+\n','\n',s)
def check(name,value):
 rows.append(dict(name=name,status='PASS' if value else 'FAIL'))
def clean(s):
 s=re.sub(r'/\*.*?\*/|//[^\n]*','',s,flags=re.S)
 s=re.sub(r'\\\n','',s)
 s=re.sub(r'^\s*#.*$','',s,flags=re.M)
 s=re.sub(r'__attribute__\s*\(\(aligned\(\d+\)\)\)','',s)
 return re.sub(r'__attribute__\s*\(\(.*?\)\)','',s)
prefix='typedef unsigned char uint8_t; typedef unsigned short uint16_t; typedef unsigned int uint32_t; typedef unsigned long long uint64_t; typedef int int32_t; typedef unsigned long size_t; typedef unsigned long uintptr_t; typedef int bool; typedef int SysResult_t;'
for path,types in {
 'CM7/Device/Src/ov5640_device.c':['CameraConfig_t','CameraPixelFormat_t','Device_t','DeviceLifecycleOps_t','OV5640_IO_t','OV5640_Object_t','Ov5640Device_t','Ov5640Ops_t'],
 'CM7/Service/usb_uvc_desc.c':['USBD_SpeedTypeDef','VideoStreamConfig_t'],
 'CM7/Media/Src/camera_capture.c':['CameraConfig_t','CameraFrame_t','BspDcmiTarget_t','CameraCaptureEventCallback_t','CameraCaptureStats_t','BspDcmiEvents_t'],
 'CM7/Media/Src/camera_mjpeg_fifo.c':['CameraConfig_t','CameraFrame_t','BspDcmiTarget_t','BspDcmiByteStreamOps_t'],
 'CM7/Application/video_config.c':['VideoConfig_t'],
 'Tests/MjpegSdramFifo/fifo_test.c':['CameraConfig_t','CameraFrame_t','BspDcmiTarget_t','BspDcmiByteStreamOps_t','UsbUvcPayload_t','UsbUvcPayloadPacket_t','VideoFrameView_t'],
}.items():
 s=clean(text(path));s=re.sub(r'DEVICE_EXPORT\([^;]*\);','',s)
 try:c_parser.CParser().parse(prefix+''.join('typedef int '+t+';' for t in types)+s);check('C99 grammar: '+path,True)
 except Exception as e:check('C99 grammar: '+path,False);print(e)
# Evaluate the actual C helper's boolean expression using its parsed AST.
header=c_parser.CParser().parse(prefix+clean(text('Common/Inc/camera_config.h')))
func=next(n for n in header.ext if isinstance(n,c_ast.FuncDef) and n.decl.name=='CameraConfig_IsJpegSizeSupported')
expr=func.body.block_items[0].expr
def evaluate(n,cfg):
 if isinstance(n,c_ast.ID):return cfg if n.name=='config' else None
 if isinstance(n,c_ast.Constant):return int(re.sub(r'[uUlL]+$','',n.value),0)
 if isinstance(n,c_ast.StructRef):return cfg[n.field.name]
 if isinstance(n,c_ast.BinaryOp):
  a=evaluate(n.left,cfg)
  if n.op=='&&':return bool(a) and bool(evaluate(n.right,cfg))
  if n.op=='||':return bool(a) or bool(evaluate(n.right,cfg))
  b=evaluate(n.right,cfg)
  if n.op=='==':return a==b
  if n.op=='!=':return a!=b
 raise ValueError(type(n).__name__)
for cfg,expected in [(None,False),({'width':160,'height':120},True),({'width':320,'height':240},True),({'width':320,'height':120},False),({'width':640,'height':480},False),({'width':0,'height':0},False)]:
 check('Actual size expression '+str(cfg),evaluate(expr,cfg)==expected)
component=text('CM7/Device/Components/OV5640/ov5640.c')
qvga=re.search(r'OV5640_QVGA\[\]\[2\].*?=\s*\{(.*?)\};',component,re.S).group(1)
check('ST QVGA table outputs 320x240',re.findall(r'0x([0-9A-Fa-f]+)',qvga)==['01','40','00','F0'])
device=text('CM7/Device/Src/ov5640_device.c')
check('Device passes mapped resolution', 'OV5640_Init(&s_sensor, resolution, pixel_format)' in device and '*resolution = OV5640_R320x240;' in device)
for p,call in [('CM7/Media/Src/camera_capture.c','CameraConfig_IsJpegSizeSupported(config)'),('CM7/Media/Src/camera_mjpeg_fifo.c','CameraConfig_IsJpegSizeSupported(config)'),('CM7/Service/usb_uvc_desc.c','CameraConfig_IsJpegSizeSupported(&config->camera)')]:
 check('Shared JPEG size policy: '+p,call in text(p))
desc=text('CM7/Service/usb_uvc_desc.c')
check('Descriptor width/height from input', 'UvcDesc_Write16(entry + 5U, config->camera.width)' in desc and 'UvcDesc_Write16(entry + 7U, config->camera.height)' in desc)
check('38400B at 25FPS fits slot validation',((38400+1020)//1021)*25==950)
video=text('CM7/Application/video_config.c')
check('MJPEG QVGA and original YUY2 defaults', '.width = APP_VIDEO_MJPEG_MODE ? 320U : 160U' in video and '.height = APP_VIDEO_MJPEG_MODE ? 240U : 120U' in video)
def body(source,name):
 m=re.search(r'^(?:static\s+)?(?:SysResult_t|void|bool)\s+'+name+r'\s*\([^;{}]*\)\s*\{',source,re.M)
 start=source.index('{',m.start());end=start;depth=1
 while depth:
  end+=1;depth+=(source[end]=='{')-(source[end]=='}')
 return source[start:end+1]
for path,names in {
 'CM7/Media/Src/camera_mjpeg_fifo.c':['Mjpeg_BlockComplete','Mjpeg_Publish','Mjpeg_ParseByte','CameraMjpegFifo_ReleaseFrame'],
 'CM7/Media/Src/camera_capture.c':['CameraJpeg_Validate','CameraStream_Rotate']
}.items():
 previous=subprocess.check_output(['git','show','8cb7753:'+path],cwd=ROOT).decode('gbk')
 previous=re.sub(r'\r+\n','\n',previous)
 for name in names:check('Preserved algorithm: '+name,body(previous,name)==body(text(path),name))
protected=['CM7/BSP','CM7/Core','CM4','Drivers','Middlewares','CM7/Service/usb_uvc_class.c','CM7/Service/usb_uvc_payload.c','CM7/Service/usb_uvc_payload.h','CM7/Platform']
changed=subprocess.check_output(['git','diff','--name-only','8cb7753','--',*protected],cwd=ROOT).decode().strip()
check('BSP/Core/CM4/component/USB transport unchanged',not changed)
result=dict(scope='static_syntax_expression_and_source_checks_only',compiler_run=False,executable_tests_run=False,hardware_verified=False,results=rows,pass_count=sum(r['status']=='PASS' for r in rows),fail_count=sum(r['status']=='FAIL' for r in rows))
(ROOT/'Tests/MjpegQvga/source_results.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
for row in rows:print(row['status'],row['name'])
print('Grammar checks use placeholder types; no real type checking, linking or hardware validation.')
raise SystemExit(bool(result['fail_count']))
