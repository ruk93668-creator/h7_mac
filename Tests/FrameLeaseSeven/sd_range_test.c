//真实BSP、HAL IDMA和IRQ路径，只有卡响应与内存介质使用原测试桩
#include "../SdmmcStage08B/idma_test.c"
#include "camera_mjpeg_fifo.h"
uint32_t SdmmcRange_TestRun(uint32_t id)
{
 uint32_t i,before,count=1U;uint8_t *p=(uint8_t *)CAMERA_MJPEG_FIFO_BASE;SysResult_t r;
 if(id==0){CHECK(Sdmmc_CpuBufferValid(p,512,true));CHECK(Sdmmc_CpuBufferValid(p,512,false));return 0;}
 if(id==1){CHECK(Sdmmc_CpuBufferValid((void *)(BSP_SDRAM_END-512),512,true));CHECK(!Sdmmc_CpuBufferValid((void *)(BSP_SDRAM_END-511),512,true));return 0;}
 if(id==2){CHECK(!Sdmmc_CpuBufferValid((void *)BSP_SDRAM_END,512,true));CHECK(!Sdmmc_CpuBufferValid((void *)(BSP_SDRAM_BASE-1),512,true));return 0;}
 if(id==3){CHECK(!Sdmmc_CpuBufferValid((void *)0xFFFFFFF0UL,512,true));CHECK(!Sdmmc_CpuBufferValid(NULL,512,true));CHECK(!Sdmmc_CpuBufferValid(p,0,true));return 0;}
 if(id==4){CHECK(!Sdmmc_CpuBufferValid((void *)BSP_SDMMC_DMA_BASE,512,true));CHECK(!Sdmmc_CpuBufferValid((void *)0x40000000UL,512,true));return 0;}
 if(id==5){CHECK(Sdmmc_CpuBufferValid((void *)0x08000000UL,512,true));CHECK(!Sdmmc_CpuBufferValid((void *)0x08000000UL,512,false));return 0;}
 if(id==6){CHECK(Sdmmc_CpuBufferValid((void *)0x20000000UL,512,true));CHECK(!Sdmmc_CpuBufferValid((void *)0x2001FF00UL,512,true));return 0;}
 CHECK(Sdmmc08B_TestRun(0)==0);before=s_starts;
 if(id==8||id==11)p+=3;
 if(id==9)p=(uint8_t *)(BSP_SDRAM_END-512);
 if(id==10){CHECK(BSP_SDMMC_WriteBlocks(16,1,(void *)(BSP_SDRAM_END-511),200)==SYS_ERR_INVALID_ARG);CHECK(s_starts==before);return 0;}
 if(id==12)count=65;
 if(id!=11)for(i=0;i<count*512;i++)p[i]=(uint8_t)((16U*512U+i)^0x5AU);
 r=id==11?BSP_SDMMC_ReadBlocks(16,count,p,200):BSP_SDMMC_WriteBlocks(16,count,p,200);
 CHECK(r==SYS_OK);CHECK(s_starts==before+(count+63)/64);CHECK(s_bad_clock_request==0);
 CHECK(SDMMC1->IDMABASE0==BSP_SDMMC_DMA_BASE);
 if(id==11)for(i=0;i<512;i++)CHECK(p[i]==(uint8_t)((16U*512U+i)^0x5AU));
 return 0;
}
