from pathlib import Path
import re,json,hashlib
root=Path(__file__).resolve().parents[2]; out=Path(__file__).resolve().parent
if (out/'audit.json').exists(): raise SystemExit('Already applied; see audit.json and before/ snapshots. Do not overwrite baseline.')
files=['CM7/Device/Src/ov5640_device.c','CM7/Media/Src/camera_capture.c','CM7/BSP/bsp_camera.c','CM7/BSP/bsp_dcmi.c','CM7/BSP/bsp_sdram.c']
for directory in ['Application','Device','BSP','Framework','Platform','Service']:
 files += [p.relative_to(root).as_posix() for p in (root/'CM4'/directory).rglob('*.c')]
comments={
'Ov5640_Init':'初始化摄像头板级接口', 'Ov5640_ReadId':'读取并检查摄像头芯片ID',
'Ov5640_Configure':'按公共配置设置传感器输出', 'Ov5640_MapPixelFormat':'将公共格式映射为组件格式',
'Ov5640_BusInit':'初始化组件使用的SCCB总线', 'Ov5640_BusTransfer':'逐字节读写SCCB寄存器并保留首次错误',
'Ov5640_BusWrite':'转发组件寄存器写入', 'Ov5640_BusRead':'转发组件寄存器读取', 'Ov5640_GetTick':'为组件提供毫秒时间',
'CameraCapture_CalcSize':'计算当前格式的完整帧字节数', 'CameraStream_Init':'初始化三缓冲区地址和状态',
'CameraStream_FindReplaceable':'查找可替换的FREE或READY缓冲区', 'CameraStream_OnFrameComplete':'处理指定DMA目标的帧完成状态',
'Camera_DelayUs':'用DWT提供软件SCCB微秒延时', 'Camera_Sda':'设置SDA电平，高电平释放开漏引脚',
'Camera_Scl':'设置软件SCCB时钟电平', 'Camera_Start':'产生软件SCCB起始条件', 'Camera_Stop':'产生软件SCCB停止条件',
'Camera_WriteByte':'发送一个字节并检查从设备应答', 'Camera_ReadByte':'读取一个字节并发送NACK',
'Dcmi_ConfigPin':'配置DCMI引脚为AF13复用', 'Dcmi_HalResult':'将HAL状态转换为通用返回码',
'Dcmi_RecordError':'锁存首次结果和错误事件，须在受保护上下文调用',
'Dcmi_CheckErrors':'检查并记录DCMI与DMA错误，须在受保护上下文调用',
'Dcmi_ClearPending':'清除DCMI、DMA和NVIC挂起标志', 'Dcmi_WaitStopped':'等待采集和DMA硬件停止', 'Dcmi_RecoverHal':'恢复DMA及DCMI句柄状态',
'Sdram_ConfigClock':'配置并检查FMC的PLL2时钟', 'Sdram_SendCommand':'发送SDRAM初始化命令', 'Sdram_Init':'初始化SDRAM时序和刷新参数',
'BSP_IIC_MapStatus':'将I2C HAL状态转换为通用返回码', 'BSP_IIC_Init':'配置I2C2时钟、句柄和滤波器',
'Oled_Init':'初始化SSD1306显示参数', 'Oled_ClearImpl':'清空OLED帧缓冲', 'Oled_DrawPixelImpl':'修改帧缓冲中的指定像素',
'Oled_RefreshImpl':'启动一帧OLED异步刷新', 'Oled_ProcessImpl':'轮询并推进OLED按页发送', 'Oled_WriteCommand':'同步发送一条OLED命令',
'App_OledRefresh':'由Timer回调启动OLED刷新，忙时跳过', 'IPC_Service_Init':'检查共享协议并标记CM4就绪',
'Framework_InitLevel':'执行指定等级的初始化，首次失败立即返回',
'Timer_HeapSwap':'交换堆节点并更新对象的反向索引', 'Timer_HeapSiftUp':'向父节点调整并恢复最小堆顺序',
'Timer_HeapSiftDown':'向子节点调整并恢复最小堆顺序', 'Timer_HeapRemove':'移除堆节点并恢复堆及反向索引',
'Timer_HandleEncode':'将池索引和代数编码为Handle', 'Timer_HandleGetIndex':'提取Handle中的池索引',
'Timer_HandleGetGeneration':'提取Handle中的代数', 'Timer_HandleValidate':'检查Handle对应对象及代数是否有效',
'Timer_DeadlineEarlier':'考虑Tick回绕，判断截止时间先后', 'Timer_DeadlineReached':'考虑Tick回绕，判断是否到期'
}
token=re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
def masked(s):return token.sub(lambda m:''.join('\n' if c=='\n' else ' ' for c in m.group()),s)
pattern=re.compile(r'^(?P<sig>(?P<static>static[ \t]+)?[A-Za-z_][\w \t*]*?\b(?P<name>[A-Za-z_]\w*)\s*\([^{};]*?\))[ \t\n]*(?P<tail>[{;])',re.M)
def funcs(s):
 msk=masked(s); result=[]
 for m in pattern.finditer(msk):
  if m['tail']!='{':continue
  start=m.start(); op=m.end()-1; depth=1;end=op+1
  while depth:
   if msk[end]=='{':depth+=1
   elif msk[end]=='}':depth-=1
   end+=1
  result.append({'name':m['name'],'start':start,'end':end,'signature':s[start:op].rstrip(),'body':s[op:end],'static':bool(m['static'])})
 return result
def comment_start(s,pos):
 # Move only adjacent whole-line comments; never consume preceding code.
 start=s.rfind('\n',0,pos)+1
 while True:
  prefix=s[:start].rstrip();end=len(prefix)
  found=None
  for t in token.finditer(prefix):
   if t.end()==end and t.group().startswith(('//','/*')):found=t
  if found is None:break
  line=s.rfind('\n',0,found.start())+1
  if s[line:found.start()].strip():break
  start=line
 return start
report=[]
for name in files:
 p=root/name;raw=p.read_bytes();s=raw.decode('gbk').replace('\r\n','\n');definitions=funcs(s);private=[f for f in definitions if f['static']]
 if not private:continue
 backup=out/'before'/name;backup.parent.mkdir(parents=True,exist_ok=True);backup.write_bytes(raw)
 names={f['name'] for f in private}; declarations=[];cuts=[];blocks=[]
 for f in private:
  assert f['name'] in comments,f['name']
  a=comment_start(s,f['start']);cuts.append((a,f['end']));blocks.append(s[a:f['end']].strip())
  declarations.append('//'+comments[f['name']]+'\n'+f['signature']+';')
 for m in pattern.finditer(masked(s)):
  if m['static'] and m['tail']==';':
   if any(a<=m.start()<b for a,b in cuts):continue
   a=comment_start(s,m.start());cuts.append((a,m.end()))
   if m['name'] not in names:
    assert m['name'] in comments,m['name']
    declarations.append('//'+comments[m['name']]+'\n'+s[m.start():m.end()].strip())
 remaining=s
 for a,b in sorted(cuts,reverse=True):remaining=remaining[:a]+remaining[b:]
 if name=='CM7/Device/Src/ov5640_device.c':
  a=remaining.index('//复用旧组件');b=remaining.index('//按旧工程',a)
  globals1=remaining[a:b].strip();remaining=remaining[:a]+remaining[b:]
  a=remaining.index('//按旧工程');globals2=remaining[a:].strip();remaining=remaining[:a]
  point=remaining.index('//绑定设备生命周期')
  remaining=remaining[:point]+globals1+'\n\n'+globals2+'\n\n'+remaining[point:]
  point=remaining.index('//绑定设备生命周期')
 elif name=='CM4/Device/Src/oled_device.c':point=remaining.index('//通用生命周期回调')
 else:
  public=funcs(remaining)
  if public:point=comment_start(remaining,public[0]['start'])
  else:point=remaining.index('FRAMEWORK_INIT_REGISTER(')
 result=remaining[:point].rstrip()+'\n\n//内部功能函数声明\n'+'\n\n'.join(declarations)+'\n\n'+remaining[point:].lstrip()
 result=result.rstrip()+'\n\n//内部功能函数实现\n'+'\n\n'.join(blocks)+'\n'
 result=re.sub(r'\n{4,}','\n\n\n',result)
 newdefs=funcs(result)
 assert {f['name']:(f['signature'],f['body']) for f in definitions}=={f['name']:(f['signature'],f['body']) for f in newdefs},name
 p.write_bytes(result.replace('\n','\r\n').encode('gbk'))
 report.append({'file':name,'internal_functions':[f['name'] for f in private],'declarations':len(declarations),'all_function_bodies_identical':True,'before_sha256':hashlib.sha256(raw).hexdigest(),'after_sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
(out/'audit.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'files':len(report),'internal_functions':sum(len(x['internal_functions']) for x in report),'all_function_bodies_identical':True},ensure_ascii=False))
