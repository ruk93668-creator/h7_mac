"""Stage02-specific ownership, unchanged algorithms and linked firmware audit."""
from pathlib import Path
import subprocess,re,json,hashlib,sys,struct,xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
BASE='4d463ba';sys.path.insert(0,str(ROOT/'Tests/OledAsyncAcceptance/python_deps'))
from elftools.elf.elffile import ELFFile
checks=[]
def check(name,value,detail=None):checks.append({'name':name,'pass':bool(value),'detail':detail})
def git(*args):return subprocess.check_output(['git',*args],cwd=ROOT)
def decode(data):
    try:return data.decode('utf-8').replace('\r\n','\n')
    except UnicodeDecodeError:return data.decode('gbk').replace('\r\n','\n')
def source(p):return decode((ROOT/p).read_bytes())
def before(p):return decode(git('show',BASE+':'+p))
token=re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[A-Za-z_]\w*|\d+|\S',re.S)
def tokens(s):return [m.group() for m in token.finditer(s) if not m.group().startswith(('//','/*'))]
def function(s,name):
    m=re.search(r'(?m)^[A-Za-z_][^\n;{}]*\b'+re.escape(name)+r'\s*\([^;{}]*\)\s*\{',s)
    if m is None:raise RuntimeError(name)
    depth=1
    for t in token.finditer(s,m.end()):
        if t.group()=='{':depth+=1
        elif t.group()=='}':
            depth-=1
            if depth==0:return s[m.start():t.end()]
    raise RuntimeError(name)
protected=['Drivers','Middlewares','CM4','Board','CM7/Device','CM7/BSP/bsp_usb.c',
           'CM7/BSP/bsp_usb.h','CM7/BSP/bsp_camera.c','CM7/BSP/bsp_sdram.c',
           'CM7/Service/usb_uvc_payload.c','CM7/Service/usb_uvc_payload.h','CM7/Service/usb_uvc_desc.c',
           'CM7/Core/Src/main.c','CM7/Platform/platform_rtos.c','CM7/Platform/FreeRTOSConfig.h','Common']
bad=git('diff',BASE,'--name-only','--',*protected).decode().splitlines()
check('Official drivers/core/port, BSP USB, CM4 and protected startup unchanged',not bad,bad)
check('Historical test source/assertions and reports unchanged',not git('diff',BASE,'--','Tests',':!Tests/NativeRuntimeStage02').strip())
bad=[]
for folder in ['CM7/BSP','CM7/Device','CM7/Media','CM7/Service','Common']:
 for p in (ROOT/folder).rglob('*'):
  if p.suffix in ['.c','.h'] and re.search(r'#\s*include\s*[<"](?:FreeRTOS|task|queue|semphr)\.h|\b(?:TaskHandle_t|QueueHandle_t|StaticTask_t)\b',source(p.relative_to(ROOT))):bad.append(str(p.relative_to(ROOT)))
check('Lower layers contain no RTOS task or queue types',not bad,bad)
unchanged={
 'CM7/BSP/bsp_dcmi.c':['BSP_DCMI_StartSnapshot','BSP_DCMI_StartContinuous','BSP_DCMI_Stop','BSP_DCMI_ReplaceTarget','Dcmi_StreamTryPair','Dcmi_StreamM0Complete','Dcmi_StreamM1Complete','DMA1_Stream0_IRQHandler','DCMI_IRQHandler'],
 'CM7/Media/Src/camera_capture.c':['CameraStream_Rotate','CameraStream_GetFrame','CameraStream_ReleaseFrame','CameraCapture_Start','CameraCapture_Init','CameraCapture_StartStream','CameraCapture_Stop','CameraCapture_Process','CameraCapture_StreamGetFrame','CameraCapture_StreamReleaseFrame'],
 'CM7/Service/usb_uvc_class.c':['UVC_DecodeControl','UVC_EncodeControl','UVC_ValidateControl','UVC_ProbeSet','UVC_CommitSet','UVC_Setup','UVC_EP0_RxReady','UVC_EP0_TxSent','UVC_SetStreamAlt','UVC_DataIn','UVC_Sof','UVC_Incomplete','UVC_ReleaseFrame'],
 'CM7/Platform/usb_device.c':['Usb_Setup','USBD_LL_Transmit','USBD_LL_PrepareReceive','USBD_LL_OpenEP','USBD_LL_CloseEP','USBD_LL_FlushEP','USBD_LL_DeInit']}
algorithm_details=[]
for p,names in unchanged.items():
 for name in names:
  a=tokens(function(before(p),name));b=tokens(function(source(p),name))
  algorithm_details.append({'file':p,'function':name,'tokens_identical':a==b})
check('Hardware timing, triple-buffer, EP0 and UVC algorithms unchanged',all(x['tokens_identical'] for x in algorithm_details),algorithm_details)
p='CM7/Service/usb_uvc_class.c';now=function(source(p),'USB_UVC_Process').replace('s_total_completed_frames++;','')
check('UVC scheduler differs only by EOF measurement counter',tokens(now)==tokens(function(before(p),'USB_UVC_Process')))
control=source('CM7/Application/Tasks/control_task.c')
check('Control never calls Media, Device or USB data-plane APIs',not re.search(r'\b(?:CameraCapture_|Device_|USB_Device_|USB_UVC_|App_Camera|USB_Mode_)\w*\s*\(',control))
camera=source('CM7/Application/Tasks/camera_task.c');usb=source('CM7/Application/Tasks/usb_task.c')
check('Camera task does not operate USB',not re.search(r'\bUSB_\w+\s*\(',camera))
check('USB task does not operate Media/Device',not re.search(r'\b(?:CameraCapture_|Device_|App_Camera)\w*\s*\(',usb))
messages=source('CM7/Application/app_messages.c')
check('All application queues use static storage', 'xQueueCreateStatic' in messages and not re.search(r'\b(?:xQueueCreate|pvPortMalloc|malloc)\s*\(',messages))
check('Native runtime never uses legacy polling',not any(re.search(r'\b(?:Main_Run|App_Run|Framework_Run|App_UvcBridgeProcess)\s*\(',source(p)) for p in ['CM7/Core/Src/main.c','CM7/Application/Tasks/camera_task.c','CM7/Application/Tasks/usb_task.c','CM7/Application/Tasks/control_task.c']))
main=source('CM7/Core/Src/main.c')
check('Initcall remains before Scheduler',main.index('Framework_InitRuntime()')<main.index('Platform_RtosStart()'))
variants=[]
for variant,output in [('cm7_test',4),('cm7_uvc',1)]:
 p=ROOT/'tmp/native_runtime_stage02'/ (variant+'.axf')
 with p.open('rb') as f:
  elf=ELFFile(f);syms={s.name:s for s in elf.get_section_by_name('.symtab').iter_symbols()}
  def data_at(address,size):
   for section in elf.iter_sections():
    if section['sh_type']=='SHT_PROGBITS' and section['sh_addr']<=address and address+size<=section['sh_addr']+section['sh_size']:
     return section.data()[address-section['sh_addr']:address-section['sh_addr']+size]
   raise RuntimeError(hex(address))
  cfg=syms['s_default_config'];raw=data_at(cfg['st_value'],cfg['st_size'])
  check(variant+' actual compiled video configuration',len(raw)==10 and struct.unpack_from('<HH',raw)==(160,120) and raw[4]==2 and raw[6]==output and struct.unpack_from('<H',raw,8)[0]==25,raw.hex())
  expected={'s_camera_stack':6144,'s_usb_stack':8192,'s_control_stack':3072,'s_idle_stack':1024,'s_message_storage':720,'s_message_queues':360}
  ranges=[]
  for name,size in expected.items():
   sym=syms[name];start=sym['st_value'];end=start+sym['st_size']
   check(variant+' '+name, sym['st_size']==size and start%8==0 and 0x20000000<=start<end<=0x20020000,{'address':hex(start),'size':sym['st_size']})
   ranges.append((start,end,name))
  ranges.sort();check(variant+' static resources do not overlap',all(a[1]<=b[0] for a,b in zip(ranges,ranges[1:])))
  check(variant+' no legacy runtime or allocator linked',not any(n in syms for n in ['Rtos_LegacyTask','Main_Run','App_Run','Framework_Run','App_UvcBridgeProcess','malloc','pvPortMalloc','s_frame','image','g_native_observation']))
  for index,name in [(11,'SVC_Handler'),(14,'PendSV_Handler'),(15,'SysTick_Handler')]:
   value=int.from_bytes(data_at(0x08000000+index*4,4),'little')
   check(variant+' '+name+' vector',(value&~1)==(syms[name]['st_value']&~1),hex(value))
  variants.append({'variant':variant,'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'watch_symbols':{n:hex(syms[n]['st_value']) for n in ['g_camera_task_debug','g_usb_task_debug','g_control_task_debug','g_bridge_debug','s_camera_bridge','s_usb_bridge']}})
check('SDRAM buffers fit MPU window',0xD0000400%32==0 and 40960%32==0 and 0xD0000400+3*40960<=0xD0020000,{'buffers':['0xD0000400','0xD000A400','0xD0014400'],'end_exclusive':'0xD001E400','mpu_end_exclusive':'0xD0020000'})
check('TEST/UVC/CM4 full rebuilds clean',all('0 Error(s), 0 Warning(s).' in (HERE/n).read_text(errors='replace') for n in ['cm7_test_rebuild.log','cm7_uvc_rebuild.log','cm4_rebuild.log']))
candidate=ET.fromstring((ROOT/'tmp/native_runtime_stage02/project_for_commit.uvprojx').read_bytes())
original=ET.fromstring(git('show',BASE+':MDK-ARM/h7_mac.uvprojx'))
def canonical(node):return (node.tag,(node.text or '').strip(),tuple(sorted(node.attrib.items())),tuple(canonical(c) for c in node))
def cm4(root):return next(x for x in root.findall('./Targets/Target') if x.findtext('TargetName')=='h7_mac_CM4')
for tree in [candidate,original]:
 t=cm4(tree)
 for g in list(t.find('Groups')):
  if g.findtext('GroupName')=='CM7 Native Runtime':t.find('Groups').remove(g)
check('CM4 configuration otherwise unchanged',canonical(cm4(candidate))==canonical(cm4(original)))
record={'baseline':git('rev-parse',BASE).decode().strip(),'checks':checks,'passed':sum(x['pass'] for x in checks),'failed':sum(not x['pass'] for x in checks),'firmware':variants,'hardware_verified':False}
(HERE/'audit_results.json').write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf-8')
for row in checks:print('PASS' if row['pass'] else 'FAIL',row['name'])
raise SystemExit(bool(record['failed']))
