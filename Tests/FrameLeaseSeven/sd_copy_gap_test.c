//真实BSP和HAL，软件测试只模拟卡响应及等待唤醒
#include "../SdmmcStage08B/idma_test.c"
static uint32_t copy_mode, copy_waits, copy_bad_mask;
volatile uint32_t copy_manual_clock;

//模拟阻塞等待，验证残留通知不能跳过实际间隔
static SysResult_t Copy_Wait(void *context, uint32_t ms)
{
 (void)context;copy_waits++;
 if(__get_PRIMASK()!=0U)copy_bad_mask++;
 if(copy_mode==1U)return SYS_OK;
 if(copy_mode==2U)return SYS_ERR_IO;
 if(copy_mode==3U&&copy_waits<=2U)return SYS_OK;
 s_tick+=ms;return SYS_ERR_TIMEOUT;
}

//执行分段复制、统一超时及IDMA批次不变的验证
uint32_t SdCopyGap_TestRun(uint32_t id)
{
 uint8_t *p=(uint8_t *)0xD0480000UL;
 uint32_t i,before,bytes=16384U;
 SysResult_t result;
 CHECK(Sdmmc08B_TestRun(0)==0);
 before=s_starts;s_wait_ops.wait=Copy_Wait;copy_manual_clock=1U;
 if(id==1)bytes=512U;
 if(id==2)bytes=16387U;
 for(i=0;i<32768U;i++){p[i]=(uint8_t)(i^0x5AU);g_sdmmc_dma_buffer[i]=0xC7U;}
 if(id>=11){
  uint8_t *d=s_cpu_buffer;
  if(id==11 || id==12){
   if(id==12)copy_mode=3;
   CHECK(BSP_SDMMC_PrepareWriteInput(d,p,8192)==SYS_OK);
   CHECK(!memcmp(d,p,8192));CHECK(s_starts==before);
   CHECK(g_sd_copy_debug.live.source==(uint32_t)p);
   CHECK(g_sd_copy_debug.live.destination==(uint32_t)d);
   CHECK(copy_waits==(SDMMC_COPY_GAP_MS==0?0U:id==12?3U:1U));return 0;
  }
  if(id==13){CHECK(BSP_SDMMC_PrepareWriteInput(d,p,8193)==SYS_ERR_INVALID_ARG);CHECK(BSP_SDMMC_PrepareWriteInput(p,p,512)==SYS_ERR_INVALID_ARG);CHECK(BSP_SDMMC_PrepareWriteInput((void *)BSP_SDMMC_DMA_BASE,p,512)==SYS_ERR_INVALID_ARG);return 0;}
  if(id==14){s_io.active=1;CHECK(BSP_SDMMC_PrepareWriteInput(d,p,1024)==SYS_ERR_BUSY);CHECK(!copy_waits);return 0;}
  if(id==15){copy_mode=1;CHECK(BSP_SDMMC_PrepareWriteInput(d,p,1024)==(SDMMC_COPY_GAP_MS?SYS_ERR_TIMEOUT:SYS_OK));return 0;}
  copy_mode=2;CHECK(BSP_SDMMC_PrepareWriteInput(d,p,1024)==(SDMMC_COPY_GAP_MS?SYS_ERR_IO:SYS_OK));return 0;
 }
 if(id==3){CHECK(Sdmmc_CopyToAxi(NULL,512,s_tick,100)==SYS_ERR_INVALID_ARG);CHECK(Sdmmc_CopyToAxi(p,32769,s_tick,100)==SYS_ERR_INVALID_ARG);CHECK(!copy_waits);return 0;}
 if(id==4){memcpy(s_cpu_buffer,p,bytes);p=s_cpu_buffer;}
 if(id==5)copy_mode=1;
 if(id==6)copy_mode=2;
 if(id==7)copy_mode=3;
 if(id==8)s_tick=UINT32_MAX-1U;
 result=Sdmmc_CopyToAxi(p,bytes,s_tick,id==9?1U:100U);
 if(SDMMC_COPY_GAP_MS!=0U&&(id==5||id==6||id==9)){
  CHECK(result==(id==6?SYS_ERR_IO:SYS_ERR_TIMEOUT));
  CHECK(!memcmp(g_sdmmc_dma_buffer,p,8192));CHECK(g_sdmmc_dma_buffer[8192]==0xC7);
 }else{
  CHECK(result==SYS_OK);CHECK(!memcmp(g_sdmmc_dma_buffer,p,bytes));
  CHECK(g_sdmmc_dma_buffer[bytes]==0xC7);
  CHECK(copy_waits==((SDMMC_COPY_GAP_MS==0U||id==4)?0U:(bytes-1U)/8192U+(id==7?2U:0U)));
 }
 CHECK(!copy_bad_mask);CHECK(s_starts==before);
 if(id==10){
  s_wait_ops.wait=Test_Wait;
  for(i=0;i<32768;i++)p[i]=(uint8_t)((16U*512U+i)^0x5AU);
  CHECK(BSP_SDMMC_WriteBlocks(16,64,p,200)==SYS_OK);
  CHECK(s_starts==before+1);CHECK(s_last_blocks==64);
  CHECK(SDMMC1->IDMABASE0==BSP_SDMMC_DMA_BASE);CHECK(!s_bad_clock_request);
 }
 return 0;
}
