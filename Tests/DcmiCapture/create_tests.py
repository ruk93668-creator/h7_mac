from pathlib import Path
import shutil
p=Path('Tests/DcmiCapture');shutil.copyfile('Tests/DcmiInit/test.sct',p/'test.sct')
s=Path('Tests/DcmiInit/dcmi_test.c').read_text(encoding='utf-8')
s=s[:s.index('static int Cases(')]
s=s.replace('static HAL_StatusTypeDef Test_DMA_DeInit(DMA_HandleTypeDef *h);','''static HAL_StatusTypeDef Test_DMA_DeInit(DMA_HandleTypeDef *h);
static HAL_StatusTypeDef Test_Start(DCMI_HandleTypeDef *h,uint32_t mode,uint32_t address,uint32_t length);
static HAL_StatusTypeDef Test_Stop(DCMI_HandleTypeDef *h);
static HAL_StatusTypeDef Test_DcmiDeInit(DCMI_HandleTypeDef *h);
static uint32_t Test_Primask(void);
static uint32_t Test_Ipsr(void);
static void Test_DisableIrq(void);
static void Test_SetPrimask(uint32_t value);''')
s=s.replace('#define HAL_DMA_DeInit Test_DMA_DeInit','''#define HAL_DMA_DeInit Test_DMA_DeInit
#define HAL_DCMI_Start_DMA Test_Start
#define HAL_DCMI_Stop Test_Stop
#define HAL_DCMI_DeInit Test_DcmiDeInit
#define __get_PRIMASK Test_Primask
#define __get_IPSR Test_Ipsr
#define __disable_irq Test_DisableIrq
#define __set_PRIMASK Test_SetPrimask''')
s=s.replace('#undef HAL_DMA_DeInit','''#undef HAL_DMA_DeInit
#undef HAL_DCMI_Start_DMA
#undef HAL_DCMI_Stop
#undef HAL_DCMI_DeInit
#undef __get_PRIMASK
#undef __get_IPSR
#undef __disable_irq
#undef __set_PRIMASK''')
s=s.replace(' int32_t result;',' int32_t result;\n uint32_t event_flags,dcmi_error,dma_error,stop_required,primask,active,start_calls,stop_calls;')
s=s.replace('static uint32_t scenario,inject,pending[2],priority[2],subpriority[2],tick;', '''static uint32_t scenario,inject,pending[2],priority[2],subpriority[2],tick;
static uint32_t mock_primask,mock_ipsr,fault_start,fault_stop,fault_dma_init,fault_dma_deinit,fault_dcmi_init,fault_dcmi_deinit;
static uint32_t arm_deferred,deferred;
volatile uint32_t g_stuck_stream;
static void DeliverDeferred(void);''')
s=s.replace(' g_result.dma_init++;',' g_result.dma_init++;\n if(fault_dma_init)return HAL_ERROR;')
s=s.replace(' g_result.dcmi_init++;',' g_result.dcmi_init++;\n if(fault_dcmi_init)return HAL_ERROR;')
s=s.replace(' g_result.dma_deinit++;',' g_result.dma_deinit++;\n if(fault_dma_deinit)return HAL_ERROR;')
s+='''
uint32_t Platform_TimeGetMs(void){return tick++;}
static uint32_t Test_Primask(void){return mock_primask;}
static uint32_t Test_Ipsr(void){return mock_ipsr;}
static void Test_DisableIrq(void){mock_primask=1;if(arm_deferred){deferred=arm_deferred;arm_deferred=0;}}
static void Test_SetPrimask(uint32_t value){mock_primask=value;if(!value && deferred)DeliverDeferred();}
static HAL_StatusTypeDef Test_Start(DCMI_HandleTypeDef *h,uint32_t mode,uint32_t address,uint32_t length)
{
 g_result.start_calls++;
 if(fault_start){h->Instance->CR|=DCMI_CR_ENABLE;return fault_start==1?HAL_ERROR:fault_start==2?HAL_BUSY:HAL_TIMEOUT;}
 return HAL_DCMI_Start_DMA(h,mode,address,length);
}
static HAL_StatusTypeDef Test_Stop(DCMI_HandleTypeDef *h)
{
 g_result.stop_calls++;
 if(fault_stop==1)return HAL_OK;
 if(fault_stop==2)return HAL_TIMEOUT;
 if(fault_stop==3)return HAL_ERROR;
 return HAL_DCMI_Stop(h);
}
static HAL_StatusTypeDef Test_DcmiDeInit(DCMI_HandleTypeDef *h){return fault_dcmi_deinit?HAL_ERROR:HAL_DCMI_DeInit(h);}
static void DeliverDeferred(void)
{
 uint32_t what=deferred;deferred=0;
 if(what==1){DMA1_Stream0->CR&=~DMA_SxCR_EN;DMA1_Stream0->NDTR=0;s_dcmi_dma.State=HAL_DMA_STATE_READY;DCMI->CR&=~DCMI_CR_CAPTURE;HAL_DCMI_FrameEventCallback(&s_dcmi);}
 else{s_dcmi.ErrorCode=HAL_DCMI_ERROR_OVR;HAL_DCMI_ErrorCallback(&s_dcmi);}
}
static void SetRaw(uint32_t flags){*((volatile uint32_t *)&DCMI->RISR)=flags;}
static void DmaComplete(void)
{
 DMA1_Stream0->CR&=~DMA_SxCR_EN;DMA1_Stream0->NDTR=0;DMA1->LISR=DMA_FLAG_TCIF0_4;
 DMA1_Stream0_IRQHandler();
}
static void FrameComplete(void)
{
 DCMI->CR&=~DCMI_CR_CAPTURE;SetRaw(DCMI_FLAG_FRAMERI);DmaComplete();DCMI_IRQHandler();
}
static int Take(uint32_t flags,SysResult_t result)
{
 BspDcmiEvents_t event;
 EQ(BSP_DCMI_TakeEvents(&event),1);EQ(event.flags,flags);EQ(event.result,result);
 g_result.event_flags=event.flags;g_result.dcmi_error=event.dcmi_error;g_result.dma_error=event.dma_error;
 EQ(BSP_DCMI_TakeEvents(&event),0);return 0;
}
static int Stopped(void)
{
 EQ(DMA1_Stream0->CR&DMA_SxCR_EN,0);EQ(DCMI->CR&(DCMI_CR_CAPTURE|DCMI_CR_ENABLE),0);
 EQ(s_dcmi.State,HAL_DCMI_STATE_READY);EQ(s_dcmi_dma.State,HAL_DMA_STATE_READY);
 EQ(s_dcmi.Lock,HAL_UNLOCKED);EQ(s_dcmi_dma.Lock,HAL_UNLOCKED);EQ(s_stop_required,0);
 EQ(s_dcmi.DMA_Handle==&s_dcmi_dma,1);EQ(s_dcmi_dma.Parent==&s_dcmi,1);
 return 0;
}
static int Cases(uint32_t id)
{
 uint32_t n;
 uint32_t *buffer=(uint32_t *)0xD0000000;
 BspDcmiEvents_t event;
 DCMI_HandleTypeDef other={0};
 scenario=0;Seed();
 if(id==0){EQ(BSP_DCMI_StartSnapshot(buffer,4800),SYS_ERR_INVALID_STATE);EQ(BSP_DCMI_Stop(),SYS_ERR_INVALID_STATE);EQ(BSP_DCMI_TakeEvents(&event),0);return 0;}
 EQ(Framework_InitRuntime(),SYS_OK);EQ(Config(),0);EQ(g_result.start_calls,0);
 if(id==1)return 0;
 if(id==2){
  EQ(BSP_DCMI_StartSnapshot(NULL,4800),SYS_ERR_INVALID_ARG);EQ(BSP_DCMI_StartSnapshot((uint32_t *)0xD0000001,4800),SYS_ERR_INVALID_ARG);
  EQ(BSP_DCMI_StartSnapshot(buffer,0),SYS_ERR_INVALID_ARG);EQ(BSP_DCMI_StartSnapshot(buffer,65536),SYS_ERR_INVALID_ARG);
  EQ(BSP_DCMI_StartSnapshot((uint32_t *)0xFFFFFFFC,2),SYS_ERR_INVALID_ARG);EQ(g_result.start_calls,0);return 0;
 }
 if(id==3){mock_ipsr=11;EQ(BSP_DCMI_StartSnapshot(buffer,1),SYS_ERR_INVALID_STATE);EQ(BSP_DCMI_Stop(),SYS_ERR_INVALID_STATE);return 0;}
 if(id==4){mock_primask=1;EQ(BSP_DCMI_StartSnapshot(buffer,1),SYS_ERR_INVALID_STATE);EQ(BSP_DCMI_Stop(),SYS_ERR_INVALID_STATE);EQ(mock_primask,1);return 0;}
 if(id==5){EQ(BSP_DCMI_Stop(),SYS_OK);EQ(Stopped(),0);return 0;}
 if(id==6){
  s_event_flags=BSP_DCMI_EVENT_ERROR;s_event_result=SYS_ERR_IO;s_event_dcmi_error=0x80;s_event_dma_error=0x40;
  s_dcmi.ErrorCode=0x80;s_dcmi_dma.ErrorCode=0x40;
  SetRaw(31);DMA1->LISR=63;pending[0]=pending[1]=1;
 }
 if(id>=18 && id<=20)fault_start=id-17;
 EQ(BSP_DCMI_StartSnapshot(buffer,4800),id>=18&&id<=20?(id==18?SYS_ERR_IO:id==19?SYS_ERR_BUSY:SYS_ERR_TIMEOUT):SYS_OK);
 if(id>=18 && id<=20){
  EQ(s_stop_required,1);EQ(DCMI->CR&(DCMI_CR_ENABLE|DCMI_CR_CAPTURE),0);
  EQ(Take(BSP_DCMI_EVENT_ERROR,id==18?SYS_ERR_IO:id==19?SYS_ERR_BUSY:SYS_ERR_TIMEOUT),0);
  fault_start=0;EQ(BSP_DCMI_Stop(),SYS_OK);EQ(Stopped(),0);EQ(BSP_DCMI_StartSnapshot(buffer,4800),SYS_OK);return 0;
 }
 EQ(DMA1_Stream0->NDTR,4800);EQ(DMA1_Stream0->PAR,(uint32_t)&DCMI->DR);EQ(DMA1_Stream0->M0AR,(uint32_t)buffer);
 EQ(DMA1_Stream0->CR&DMA_SxCR_EN,DMA_SxCR_EN);EQ(DCMI->CR&(DCMI_CR_ENABLE|DCMI_CR_CAPTURE),DCMI_CR_ENABLE|DCMI_CR_CAPTURE);
 EQ(DCMI->IER&(DCMI_IT_ERR|DCMI_IT_OVR),DCMI_IT_ERR|DCMI_IT_OVR);EQ(DCMI->IER&DCMI_IT_FRAME,0);EQ(mock_primask,0);
 if(id==6){EQ(s_event_flags,0);EQ(s_dcmi.ErrorCode,0);EQ(s_dcmi_dma.ErrorCode,0);EQ(DCMI->RISR,0);EQ(DMA1->LISR,0);EQ(BSP_DCMI_TakeEvents(&event),0);return 0;}
 if(id==7){n=g_result.start_calls;EQ(BSP_DCMI_StartSnapshot(buffer,4800),SYS_ERR_BUSY);EQ(g_result.start_calls,n);return 0;}
 if(id==8){DmaComplete();EQ(s_dcmi.State,HAL_DCMI_STATE_READY);EQ(BSP_DCMI_TakeEvents(&event),0);EQ(BSP_DCMI_StartSnapshot(buffer,4800),SYS_ERR_BUSY);return 0;}
 if(id==9){FrameComplete();EQ(Take(BSP_DCMI_EVENT_FRAME,SYS_OK),0);return 0;}
 if(id==10){FrameComplete();EQ(DCMI->IER&(DCMI_IT_ERR|DCMI_IT_OVR),0);EQ(Take(BSP_DCMI_EVENT_FRAME,SYS_OK),0);EQ(BSP_DCMI_StartSnapshot(buffer,4800),SYS_OK);FrameComplete();EQ(Take(BSP_DCMI_EVENT_FRAME,SYS_OK),0);return 0;}
 if(id==11 || id==12){
  SetRaw(id==11?DCMI_FLAG_ERRRI:DCMI_FLAG_OVRRI);DCMI_IRQHandler();
  EQ(s_dcmi_dma.State,HAL_DMA_STATE_ABORT);EQ(Take(BSP_DCMI_EVENT_ERROR,SYS_ERR_IO),0);
  EQ(BSP_DCMI_StartSnapshot(buffer,4800),SYS_ERR_INVALID_STATE);
  EQ(BSP_DCMI_Stop(),SYS_OK);EQ(Stopped(),0);EQ(BSP_DCMI_StartSnapshot(buffer,4800),SYS_OK);return 0;
 }
 if(id==13){FrameComplete();/* completion not yet consumed */SetRaw(DCMI_FLAG_ERRRI|DCMI_FLAG_FRAMERI);DCMI_IRQHandler();EQ(Take(BSP_DCMI_EVENT_ERROR,SYS_ERR_IO),0);EQ(g_result.dcmi_error&HAL_DCMI_ERROR_SYNC,HAL_DCMI_ERROR_SYNC);return 0;}
 if(id==14){DMA1->LISR=DMA_FLAG_TEIF0_4;DMA1_Stream0_IRQHandler();EQ(Take(BSP_DCMI_EVENT_ERROR,SYS_ERR_IO),0);EQ(g_result.dma_error&HAL_DMA_ERROR_TE,HAL_DMA_ERROR_TE);EQ(BSP_DCMI_Stop(),SYS_OK);EQ(Stopped(),0);return 0;}
 if(id==15){s_dcmi_dma.ErrorCode=HAL_DMA_ERROR_FE;HAL_DCMI_ErrorCallback(&s_dcmi);EQ(Take(BSP_DCMI_EVENT_ERROR,SYS_ERR_IO),0);EQ(g_result.dma_error,HAL_DMA_ERROR_FE);return 0;}
 if(id==16){HAL_DCMI_FrameEventCallback(&other);HAL_DCMI_ErrorCallback(&other);EQ(BSP_DCMI_TakeEvents(&event),0);return 0;}
 if(id==17){HAL_DCMI_FrameEventCallback(&s_dcmi);EQ(Take(BSP_DCMI_EVENT_ERROR,SYS_ERR_PROTOCOL),0);return 0;}
 if(id==21){SetRaw(DCMI_FLAG_ERRRI);DCMI_IRQHandler();/* no abort IRQ delivered */EQ(BSP_DCMI_Stop(),SYS_OK);EQ(Stopped(),0);EQ(Take(BSP_DCMI_EVENT_ERROR,SYS_ERR_IO),0);return 0;}
 if(id==22){s_dcmi_dma.State=HAL_DMA_STATE_READY;/* stale HAL state while DMA EN=1 */fault_stop=1;EQ(BSP_DCMI_Stop(),SYS_OK);EQ(Stopped(),0);return 0;}
 if(id==23 || id==24){fault_stop=id==23?2:3;EQ(BSP_DCMI_Stop(),id==23?SYS_ERR_TIMEOUT:SYS_ERR_IO);EQ(Stopped(),0);EQ(BSP_DCMI_StartSnapshot(buffer,4800),SYS_OK);return 0;}
 if(id==25){
  g_stuck_stream=1;EQ(BSP_DCMI_Stop(),SYS_ERR_TIMEOUT);EQ(s_stop_required,1);EQ(g_result.nvic_enabled,0);
  EQ(BSP_DCMI_StartSnapshot(buffer,4800),SYS_ERR_INVALID_STATE);EQ(Take(BSP_DCMI_EVENT_ERROR,SYS_ERR_TIMEOUT),0);
  g_stuck_stream=0;EQ(BSP_DCMI_Stop(),SYS_OK);EQ(Stopped(),0);return 0;
 }
 if(id>=26 && id<=29){
  if(id==26)fault_dma_deinit=1;if(id==27)fault_dma_init=1;if(id==28)fault_dcmi_deinit=1;if(id==29)fault_dcmi_init=1;
  EQ(BSP_DCMI_Stop(),SYS_ERR_IO);EQ(g_result.nvic_enabled,0);EQ(s_stop_required,1);
  fault_dma_deinit=fault_dma_init=fault_dcmi_deinit=fault_dcmi_init=0;EQ(BSP_DCMI_Stop(),SYS_OK);EQ(Stopped(),0);return 0;
 }
 if(id==30){s_dcmi_dma.State=HAL_DMA_STATE_ABORT;s_dcmi_dma.Lock=HAL_LOCKED;EQ(BSP_DCMI_Stop(),SYS_OK);EQ(Stopped(),0);return 0;}
 if(id==31){EQ(BSP_DCMI_Stop(),SYS_OK);HAL_DCMI_FrameEventCallback(&s_dcmi);s_dcmi.ErrorCode=HAL_DCMI_ERROR_OVR;HAL_DCMI_ErrorCallback(&s_dcmi);EQ(BSP_DCMI_TakeEvents(&event),0);return 0;}
 if(id==32){FrameComplete();EQ(BSP_DCMI_TakeEvents(NULL),0);EQ(Take(BSP_DCMI_EVENT_FRAME,SYS_OK),0);return 0;}
 if(id==33){arm_deferred=1;EQ(BSP_DCMI_TakeEvents(&event),0);EQ(Take(BSP_DCMI_EVENT_FRAME,SYS_OK),0);EQ(mock_primask,0);return 0;}
 if(id==34){FrameComplete();arm_deferred=2;EQ(BSP_DCMI_TakeEvents(&event),1);EQ(event.flags,BSP_DCMI_EVENT_FRAME);EQ(Take(BSP_DCMI_EVENT_ERROR,SYS_ERR_IO),0);return 0;}
 if(id==35){
  FrameComplete();mock_primask=1;arm_deferred=2;EQ(BSP_DCMI_TakeEvents(&event),1);EQ(event.flags,BSP_DCMI_EVENT_FRAME);EQ(mock_primask,1);EQ(deferred,2);
  Test_SetPrimask(0);EQ(Take(BSP_DCMI_EVENT_ERROR,SYS_ERR_IO),0);return 0;
 }
 if(id==36){FrameComplete();SetRaw(DCMI_FLAG_OVRRI);EQ(Take(BSP_DCMI_EVENT_ERROR,SYS_ERR_IO),0);EQ(g_result.dcmi_error&HAL_DCMI_ERROR_OVR,HAL_DCMI_ERROR_OVR);return 0;}
 if(id==37){FrameComplete();EQ(BSP_DCMI_Stop(),SYS_OK);EQ(Take(BSP_DCMI_EVENT_FRAME,SYS_OK),0);return 0;}
 if(id==38){s_control_busy=true;EQ(BSP_DCMI_Stop(),SYS_ERR_BUSY);EQ(BSP_DCMI_StartSnapshot(buffer,4800),SYS_ERR_BUSY);s_control_busy=false;return 0;}
 if(id==39){SetRaw(DCMI_FLAG_ERRRI);DCMI_IRQHandler();DMA1->LISR=DMA_FLAG_TCIF0_4;DMA1_Stream0_IRQHandler();EQ(Take(BSP_DCMI_EVENT_ERROR,SYS_ERR_IO),0);EQ(BSP_DCMI_Stop(),SYS_OK);EQ(Stopped(),0);return 0;}
 if(id==40){FrameComplete();EQ(Take(BSP_DCMI_EVENT_FRAME,SYS_OK),0);s_dcmi.Lock=HAL_LOCKED;EQ(BSP_DCMI_StartSnapshot(buffer,4800),SYS_ERR_BUSY);EQ(BSP_DCMI_Stop(),SYS_ERR_BUSY);EQ(Stopped(),0);return 0;}
 if(id==41){tick=0xFFFFFFF0U;g_stuck_stream=1;EQ(BSP_DCMI_Stop(),SYS_ERR_TIMEOUT);EQ(s_stop_required,1);return 0;}
 if(id==42){FrameComplete();EQ(BSP_DCMI_StartSnapshot(buffer,4800),SYS_OK);EQ(BSP_DCMI_TakeEvents(&event),0);return 0;}
 if(id==43){SetRaw(DCMI_FLAG_ERRRI);DCMI_IRQHandler();EQ(Take(BSP_DCMI_EVENT_ERROR,SYS_ERR_IO),0);HAL_DCMI_FrameEventCallback(&s_dcmi);EQ(BSP_DCMI_TakeEvents(&event),0);return 0;}
 if(id==44){FrameComplete();EQ(BSP_DCMI_StartSnapshot(NULL,4800),SYS_ERR_INVALID_ARG);EQ(Take(BSP_DCMI_EVENT_FRAME,SYS_OK),0);return 0;}
 if(id==45){EQ(BSP_DCMI_Stop(),SYS_OK);EQ(BSP_DCMI_TakeEvents(&event),0);EQ(BSP_DCMI_Stop(),SYS_OK);EQ(BSP_DCMI_TakeEvents(&event),0);return 0;}
 return 0;
}
int Dcmi_TestRun(uint32_t id)
{
 int result=Cases(id);
 g_result.dma_cr=DMA1_Stream0->CR;g_result.dcmi_cr=DCMI->CR;g_result.request=s_dcmi_dma.Init.Request;g_result.ready=s_dcmi_ready;
 g_result.stop_required=s_stop_required;g_result.primask=mock_primask;g_result.active=s_capture_active;
 return result;
}
'''
(p/'dcmi_test.c').write_text(s,encoding='utf-8')
s=Path('Tests/DcmiInit/run_tests.py').read_text(encoding='utf-8-sig')
s=s.replace("'CM7/BSP','CM7/Framework'","'CM7/BSP','CM7/Platform','CM7/Framework'")
a=s.index('names=');b=s.index('\nfields=',a)
names=['未初始化拒绝调用','初始化不自动采集','参数边界和对齐','ISR拒绝启动/停止','关中断拒绝启动/停止','空闲Stop','清理旧事件和硬件挂起','Active重复Start拒绝','DMA完成尚无Frame','正常Snapshot完成','连续两帧和ERR/OVR重启','同步错先通知再Stop','溢出先通知再Stop','错误和Frame同时到达','真实DMA TE错误','纯FIFO错误回调','过滤其他句柄回调','早到Frame拒绝','Start HAL_ERROR','Start HAL_BUSY','Start HAL_TIMEOUT','Abort无回调仍能Stop恢复','HAL Stop虚假成功仍核对DMA','HAL Stop超时但恢复硬件','HAL Stop错误但恢复硬件','DMA无法停下时超时并阻止Start','恢复DMA DeInit失败','恢复DMA Init失败','恢复DCMI DeInit失败','恢复DCMI Init失败','ABORT+Lock遗留恢复','Stop后迟到回调丢弃','NULL Take不消费','读清期间完成IRQ延后交付','读清期间新错误不丢失','PRIMASK=1保持及中断延后','未处理硬件错误覆盖FRAME','Stop保留已完成事件','控制接口重入BUSY','真实异步Abort完成IRQ','遗留DCMI Lock停止恢复','Stop等待Tick回绕','新Start清除旧完成','读出错误后Frame仍不变成功','非法Start不清事件','重复Stop不产生完成事件']
s=s[:a]+'names='+repr(names)+s[b:]
s=s.replace("'ready','result']","'ready','result','event_flags','dcmi_error','dma_error','stop_required','primask','active','start_calls','stop_calls']")
s=s.replace("struct.unpack('<13Ii',u.mem_read(symbols['g_result'],56))","struct.unpack('<13Ii8I',u.mem_read(symbols['g_result'],88))")
a=s.index(" access={'sdram'");b=s.index('\n u.hook_add',a)
s=s[:a]+''' access={'sdram':0,'started':False,'model_error':None}
 def word(address):return struct.unpack('<I',u.mem_read(address,4))[0]
 def put(address,value):u.mem_write(address,struct.pack('<I',value&0xFFFFFFFF))
 def memory(uc,kind,address,size,value,data):
  if 0xD0000000<=address<0xD0010000:access['sdram']+=1
  if kind==16 and address==0x48020010:put(address,word(0x48020008)&word(0x4802000C))
  if kind==16 and address==0x40020010 and word(symbols['g_stuck_stream']):put(address,word(address)|1)
  if kind==17:
   if address==0x40020008:put(0x40020000,word(0x40020000)&~value)
   if address==0x48020014:
    put(0x48020008,word(0x48020008)&~value);put(0x48020010,word(0x48020010)&~value)
   if address==0x40020010 and value&1:access['started']=True
'''+s[b:]
s=s.replace(" and not access['started']",'')
s=s.replace('count=20000000,timeout=10000000','count=50000000,timeout=10000000')
(p/'run_tests.py').write_text(s,encoding='utf-8')
