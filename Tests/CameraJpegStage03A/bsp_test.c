//真实BSP与HAL，硬件寄存器使用Unicorn内存模型
#include "../DcmiReplaceTarget/dcmi_test.c"
static SysResult_t AcceptFrame(BspDcmiTarget_t t,void *p){(void)t;(void)p;return SYS_OK;}
static uint32_t progress_count;
static void JpegProgress(uint32_t events,void *context){(void)context;if(events&BSP_DCMI_NOTIFY_PROGRESS)progress_count++;}
int Stage02_TestRun(uint32_t id)
{
 uint32_t bytes=0x12345678U;
 Seed();if(id==41)EQ(BSP_DCMI_SetEventCallback(JpegProgress,NULL),SYS_OK);EQ(BSP_DCMI_Init(),SYS_OK);
 if(id==0){EQ(BSP_DCMI_StartJpegSnapshot(NULL,128),SYS_ERR_INVALID_ARG);EQ(BSP_DCMI_StartJpegSnapshot((uint32_t *)0xD0000401,128),SYS_ERR_INVALID_ARG);return 0;}
 if(id==1){EQ(BSP_DCMI_StartJpegSnapshot((uint32_t *)0xD0000400,0),SYS_ERR_INVALID_ARG);EQ(BSP_DCMI_StartJpegSnapshot((uint32_t *)0xD0000400,65536),SYS_ERR_INVALID_ARG);return 0;}
 if(id==2){mock_ipsr=16;EQ(BSP_DCMI_StartJpegSnapshot((uint32_t *)0xD0000400,128),SYS_ERR_INVALID_STATE);return 0;}
 if(id==3){mock_primask=1;EQ(BSP_DCMI_StartJpegSnapshot((uint32_t *)0xD0000400,128),SYS_ERR_INVALID_STATE);return 0;}
 if(id==4){EQ(BSP_DCMI_ProcessJpeg(&bytes),SYS_ERR_INVALID_STATE);EQ(BSP_DCMI_ProcessJpeg(NULL),SYS_ERR_INVALID_ARG);return 0;}
 if(id==5)fault_dma_init=1;
 if(id==6)fault_dcmi_init=1;
 if(id==7)fault_start=1;
 EQ(BSP_DCMI_StartJpegSnapshot((uint32_t *)0xD0000400,128),id>=5 && id<=7?SYS_ERR_IO:SYS_OK);
 if(id>=5 && id<=7){EQ(s_stop_required,true);EQ(bytes,0x12345678U);return 0;}
 if(id>=32 && id<=40){
  if(id==33)fault_stop=2;
  if(id==34)fault_stop=3;
  if(id==35)fault_dma_deinit=1;
  if(id==36)fault_dma_init=1;
  if(id==37)fault_dcmi_deinit=1;
  if(id==38)fault_dcmi_init=1;
  if(id==39)g_stuck_stream=1;
  if(id==40){SetRaw(DCMI_FLAG_OVRRI);Dcmi_CheckErrors();}
  if(id>=33 && id<=39){
   EQ(BSP_DCMI_Stop(),id==33 || id==39?SYS_ERR_TIMEOUT:SYS_ERR_IO);
   EQ(s_stop_required,true);EQ(s_jpeg.active,true);
   EQ(BSP_DCMI_StartJpegSnapshot((uint32_t *)0xD0000400,128),SYS_ERR_INVALID_STATE);
   fault_stop=fault_dma_deinit=fault_dma_init=fault_dcmi_deinit=fault_dcmi_init=g_stuck_stream=0;
  }
  EQ(BSP_DCMI_Stop(),SYS_OK);EQ(Stopped(),0);EQ(s_jpeg.active,false);
  HAL_DCMI_FrameEventCallback(&s_dcmi);Dcmi_JpegDmaComplete(&s_dcmi_dma);
  EQ(s_capture_active,false);EQ(s_jpeg.frame_seen,false);
  DMA1->LISR=0;SetRaw(0);
  EQ(BSP_DCMI_StartJpegSnapshot((uint32_t *)0xD0000400,128),SYS_OK);
  EQ(s_jpeg.active,true);EQ(s_jpeg.frame_seen,false);
  EQ(BSP_DCMI_Stop(),SYS_OK);EQ(BSP_DCMI_Stop(),SYS_OK);return 0;
 }
 EQ(s_jpeg.active,true);EQ(s_dcmi.Init.JPEGMode,DCMI_JPEG_ENABLE);EQ(DMA1_Stream0->FCR&DMA_SxFCR_DMDIS,0);EQ(DCMI->IER&DCMI_IT_FRAME,DCMI_IT_FRAME);
 if(id==8){EQ(BSP_DCMI_StartSnapshot((uint32_t *)0xD0000400,10),SYS_ERR_BUSY);return 0;}
 if(id==9){DCMI->CR&=~DCMI_CR_CAPTURE;EQ(BSP_DCMI_ProcessJpeg(&bytes),SYS_ERR_BUSY);EQ(bytes,0x12345678U);return 0;}
 if(id==10){DMA1_Stream0->NDTR=0;EQ(BSP_DCMI_ProcessJpeg(&bytes),SYS_ERR_NO_RESOURCE);EQ(bytes,0x12345678U);return 0;}
 if(id==11){DMA1_Stream0->NDTR=0;Dcmi_JpegDmaComplete(&s_dcmi_dma);EQ(BSP_DCMI_ProcessJpeg(&bytes),SYS_ERR_NO_RESOURCE);return 0;}
 if(id==23){DmaComplete();EQ(BSP_DCMI_ProcessJpeg(&bytes),SYS_ERR_NO_RESOURCE);EQ(BSP_DCMI_Stop(),SYS_OK);EQ(s_jpeg.active,false);EQ(s_dcmi_dma.Init.FIFOMode,DMA_FIFOMODE_ENABLE);return 0;}
 DMA1_Stream0->NDTR=100;
 if(id==25){*((volatile uint32_t *)&DCMI->MISR)=DCMI_FLAG_FRAMERI;SetRaw(DCMI_FLAG_FRAMERI);DCMI_IRQHandler();}
 else HAL_DCMI_FrameEventCallback(&s_dcmi);
 EQ(s_jpeg.frame_seen,true);EQ(s_event_flags&BSP_DCMI_EVENT_FRAME,0);
 if(id==12){EQ(BSP_DCMI_ProcessJpeg(&bytes),SYS_ERR_BUSY);EQ(s_jpeg.disabling,false);return 0;}
 DCMI->CR&=~DCMI_CR_CAPTURE;
 if(id==13){*((volatile uint32_t *)&DCMI->SR)=DCMI_SR_FNE;EQ(BSP_DCMI_ProcessJpeg(&bytes),SYS_ERR_BUSY);EQ(s_jpeg.disabling,false);return 0;}
 EQ(BSP_DCMI_ProcessJpeg(&bytes),SYS_ERR_BUSY);EQ(bytes,0x12345678U);
 if(id==41)EQ(progress_count,2);
 if(id==29){
  //软件关闭Stream产生TCIF，走真实HAL IRQ，不得成为NO_RESOURCE
  DMA1->LISR=DMA_FLAG_TCIF0_4;DMA1_Stream0_IRQHandler();
  EQ(s_stop_required,false);EQ(s_event_flags&BSP_DCMI_EVENT_ERROR,0);
  EQ(DMA1_Stream0->CR&DMA_IT_TC,0);
 }
 if(id==30){Dcmi_JpegDmaComplete(&s_dcmi_dma);EQ(s_stop_required,false);}
 if(id==31){DMA1_Stream0->NDTR=0;EQ(BSP_DCMI_ProcessJpeg(&bytes),SYS_ERR_NO_RESOURCE);return 0;}

 if(id==14){DMA1_Stream0->CR|=DMA_SxCR_EN;EQ(BSP_DCMI_ProcessJpeg(&bytes),SYS_ERR_BUSY);return 0;}
 if(id==24){DMA1->LISR=DMA_FLAG_TEIF0_4;EQ(BSP_DCMI_ProcessJpeg(&bytes),SYS_ERR_IO);EQ(bytes,0x12345678U);return 0;}
 if(id==27 || id==28){
  if(id==27)fault_dma_init=1;else fault_dcmi_init=1;
  EQ(BSP_DCMI_ProcessJpeg(&bytes),SYS_ERR_IO);EQ(bytes,0x12345678U);EQ(s_stop_required,true);
  fault_dma_init=fault_dcmi_init=0;
  EQ(BSP_DCMI_Stop(),SYS_OK);EQ(s_jpeg.active,false);EQ(BSP_DCMI_ProcessJpeg(&bytes),SYS_ERR_INVALID_STATE);return 0;
 }
 if(id==15)SetRaw(DCMI_FLAG_OVRRI);
 if(id==16)DMA1_Stream0->FCR|=DMA_SxFCR_DMDIS;
 if(id==17)DMA1_Stream0->NDTR=129;
 if(id==18)fault_stop=3;
 if(id==19)fault_dma_init=1;
 if(id>=15 && id<=19){EQ(BSP_DCMI_ProcessJpeg(&bytes)!=SYS_OK,true);EQ(bytes,0x12345678U);return 0;}
 EQ(BSP_DCMI_ProcessJpeg(&bytes),SYS_OK);EQ(bytes,112);EQ(s_jpeg.active,false);EQ(s_dcmi.Init.JPEGMode,DCMI_JPEG_DISABLE);EQ(s_dcmi_dma.Init.FIFOMode,DMA_FIFOMODE_ENABLE);EQ(s_dcmi_dma.Init.MemBurst,DMA_MBURST_INC4);EQ(s_capture_active,false);
 if(id==26){DMA1->LISR=0;EQ(BSP_DCMI_StartContinuous((uint32_t *)0xD0000400,(uint32_t *)0xD0001400,32,AcceptFrame,NULL),SYS_OK);EQ(s_dcmi.Init.JPEGMode,DCMI_JPEG_DISABLE);EQ(s_dcmi_dma.Init.FIFOMode,DMA_FIFOMODE_ENABLE);return 0;}
 if(id==21){EQ(BSP_DCMI_StartSnapshot((uint32_t *)0xD0000400,32),SYS_OK);EQ(s_dcmi.Init.JPEGMode,DCMI_JPEG_DISABLE);return 0;}
 if(id==22){HAL_DCMI_FrameEventCallback(&s_dcmi);EQ(s_event_flags&BSP_DCMI_EVENT_FRAME,0);return 0;}
 return 0;
}
