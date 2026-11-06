//编译真实BSP/HAL，验证Continuous不是逐帧Snapshot
#include "../DcmiReplaceTarget/dcmi_test.c"
int Stage02_TestRun(uint32_t id)
{
 uint32_t count=99,frames=99,i;uint8_t data[256];uint32_t *ring=(uint32_t *)0xD0300000;
 Seed();EQ(BSP_DCMI_Init(),SYS_OK);
 if(id==0){EQ(BSP_DCMI_StartJpegStream(ring,65535),SYS_ERR_INVALID_ARG);EQ(BSP_DCMI_StartJpegStream(NULL,32768),SYS_ERR_INVALID_ARG);return 0;}
 if(id==1)fault_dma_init=1;
 EQ(BSP_DCMI_StartJpegStream(ring,32768),id==1?SYS_ERR_IO:SYS_OK);
 if(id==1){EQ(s_stop_required,true);fault_dma_init=0;EQ(BSP_DCMI_Stop(),SYS_OK);return 0;}
 EQ(s_dcmi_dma.Init.FIFOMode,DMA_FIFOMODE_ENABLE);
 EQ(s_dcmi_dma.Init.FIFOThreshold,DMA_FIFO_THRESHOLD_FULL);
 EQ(DMA1_Stream0->FCR&(DMA_SxFCR_DMDIS|DMA_SxFCR_FTH),DMA_SxFCR_DMDIS|DMA_FIFO_THRESHOLD_FULL);
 if(id==17){DMA1_Stream0->FCR&=~DMA_SxFCR_DMDIS;EQ(BSP_DCMI_ReadJpegStream(data,256,&count,&frames),SYS_ERR_PROTOCOL);return 0;}
 if(id==18){DMA1_Stream0->FCR&=~DMA_SxFCR_FTH;EQ(BSP_DCMI_ReadJpegStream(data,256,&count,&frames),SYS_ERR_PROTOCOL);return 0;}
 EQ(DCMI->CR&DCMI_CR_CM,DCMI_MODE_CONTINUOUS);EQ(DCMI->CR&DCMI_CR_CAPTURE,DCMI_CR_CAPTURE);
 EQ(DMA1_Stream0->CR&DMA_SxCR_CIRC,DMA_SxCR_CIRC);EQ(DMA1_Stream0->CR&DMA_SxCR_DBM,DMA_SxCR_DBM);
 if(id==2){EQ(BSP_DCMI_ProcessJpeg(&count),SYS_ERR_INVALID_STATE);return 0;}
 if(id==3){EQ(BSP_DCMI_StartJpegSnapshot(ring,100),SYS_ERR_BUSY);EQ(BSP_DCMI_StartJpegStream(ring,32768),SYS_ERR_BUSY);return 0;}
 for(i=0;i<256;i++)((uint8_t *)ring)[i]=(uint8_t)i;
 DMA1_Stream0->NDTR=1024;
 if(id==4){DMA1_Stream0->CR|=DMA_SxCR_CT;Dcmi_JpegRingM0(&s_dcmi_dma);EQ(s_jpeg.blocks,1);EQ(DMA1_Stream0->M0AR,0xD0302000);EQ(BSP_DCMI_ReadJpegStream(data,256,&count,&frames),SYS_OK);EQ(count,256);for(i=0;i<256;i++)EQ(data[i],i);EQ(frames,0);return 0;}
 if(id==5){HAL_DCMI_FrameEventCallback(&s_dcmi);HAL_DCMI_FrameEventCallback(&s_dcmi);EQ(DCMI->IER&DCMI_IT_FRAME,DCMI_IT_FRAME);EQ(DCMI->CR&DCMI_CR_CAPTURE,DCMI_CR_CAPTURE);EQ(DMA1_Stream0->CR&DMA_SxCR_EN,DMA_SxCR_EN);EQ(g_result.start_calls,0);EQ(g_result.stop_calls,0);EQ(s_jpeg.frames,2);return 0;}
 if(id==6){DMA1->LISR=DMA_FLAG_TCIF0_4;EQ(BSP_DCMI_ReadJpegStream(data,256,&count,&frames),SYS_OK);EQ(count,0);EQ(s_jpeg.read_bytes,0);return 0;}
 if(id==7){s_jpeg.blocks=17;EQ(BSP_DCMI_ReadJpegStream(data,256,&count,&frames),SYS_ERR_NO_RESOURCE);EQ(count,0);return 0;}
 if(id==8){s_jpeg.read_bytes=131068;((uint8_t *)ring)[131068]=0xAB;s_jpeg.blocks=33;EQ(BSP_DCMI_ReadJpegStream(data,256,&count,&frames),SYS_OK);EQ(count,256);EQ(data[0],0xAB);EQ(data[4],0);return 0;}
 if(id==9){DMA1_Stream0->CR&=~DMA_SxCR_DBM;EQ(BSP_DCMI_ReadJpegStream(data,256,&count,&frames),SYS_ERR_PROTOCOL);return 0;}
 if(id==10){SetRaw(DCMI_FLAG_OVRRI);EQ(BSP_DCMI_ReadJpegStream(data,256,&count,&frames),SYS_ERR_IO);EQ(count,0);return 0;}
 if(id==11){DMA1->LISR=DMA_FLAG_DMEIF0_4;EQ(BSP_DCMI_ReadJpegStream(data,256,&count,&frames),SYS_ERR_IO);return 0;}
 if(id==12){DMA1_Stream0->NDTR=1;Dcmi_JpegRingM0(&s_dcmi_dma);EQ(s_stop_required,true);EQ(s_event_result,SYS_ERR_PROTOCOL);return 0;}
 if(id==13){fault_stop=2;EQ(BSP_DCMI_Stop(),SYS_ERR_TIMEOUT);EQ(s_jpeg.circular,true);fault_stop=0;EQ(BSP_DCMI_Stop(),SYS_OK);EQ(s_jpeg.circular,false);EQ(s_dcmi_dma.Init.Mode,DMA_NORMAL);return 0;}
 if(id==14){EQ(BSP_DCMI_ReadJpegStream(data,257,&count,&frames),SYS_ERR_INVALID_ARG);mock_ipsr=1;EQ(BSP_DCMI_ReadJpegStream(data,1,&count,&frames),SYS_ERR_INVALID_STATE);return 0;}
 if(id==15){HAL_DCMI_FrameEventCallback(&s_dcmi);EQ(BSP_DCMI_ReadJpegStream(data,0,&count,&frames),SYS_OK);EQ(count,0);EQ(frames,1);EQ(s_jpeg.read_bytes,0);return 0;}
 EQ(BSP_DCMI_Stop(),SYS_OK);EQ(Stopped(),0);EQ(s_jpeg.circular,false);EQ(BSP_DCMI_StartJpegSnapshot(ring,100),SYS_OK);EQ(DCMI->CR&DCMI_CR_CM,DCMI_MODE_SNAPSHOT);return 0;
}
