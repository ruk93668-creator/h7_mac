//使用真实复制标记和锁存实现，运行器在完整标记后模拟错误中断
#include "../SdmmcStage08B/idma_test.c"

static volatile uint32_t inject_fault;

//验证完整标记、首次锁存、半更新标记及现场不被后续复制覆盖
uint32_t SdCopyTrace_TestRun(uint32_t id)
{
 uint8_t *p=(uint8_t *)0xD0480000UL;
 uint32_t i,sequence,source;
 CHECK(Sdmmc08B_TestRun(0)==0);
 for(i=0;i<1024;i++)p[i]=(uint8_t)i;
 *(volatile uint32_t *)&DMA1->LISR=id==4?1U:0x30U;
 if(id==1||id==4)inject_fault=1;
 if(id==2){
  g_sd_copy_debug.live.sequence=3;g_sd_copy_debug.live.active=0;
  BSP_SDMMC_CaptureCopyFault(SYS_ERR_IO,2);
  CHECK(g_sd_copy_debug.fault.state==2&&g_sd_copy_debug.fault.copy_consistent==0);
  return 0;
 }
 Sdmmc_CopyMarked(g_sdmmc_dma_buffer,p,1024);
 CHECK(g_sd_copy_debug.live.active==0);
 CHECK(g_sd_copy_debug.live.source==(uint32_t)p);
 CHECK(g_sd_copy_debug.live.destination==BSP_SDMMC_DMA_BASE);
 CHECK(g_sd_copy_debug.live.bytes==1024);
 CHECK((g_sd_copy_debug.live.sequence&1U)==0);
 CHECK(!memcmp(g_sdmmc_dma_buffer,p,1024));
 if(id==0){CHECK(!g_sd_copy_debug.fault.state);CHECK(g_sd_copy_debug.live.dma_lisr_before==0x30);return 0;}
 if(id==3)BSP_SDMMC_CaptureCopyFault(SYS_ERR_TIMEOUT,0);
 CHECK(g_sd_copy_debug.fault.state==2&&g_sd_copy_debug.fault.copy_consistent==1);
 CHECK(g_sd_copy_debug.fault.copy_active==(id==3?0U:1U));
 CHECK(g_sd_copy_debug.fault.source==(uint32_t)p);
 CHECK(g_sd_copy_debug.fault.destination==BSP_SDMMC_DMA_BASE);
 CHECK(g_sd_copy_debug.fault.bytes==1024);
 CHECK(g_sd_copy_debug.fault.dma_lisr_before==(id==4?1U:0x30U));
 if(id!=3){CHECK(g_sd_copy_debug.fault.dma_lisr_fault==0x31);CHECK(g_sd_copy_debug.fault.dma_error==2);}
 sequence=g_sd_copy_debug.fault.sequence;source=g_sd_copy_debug.fault.source;
 Sdmmc_CopyMarked(g_sdmmc_dma_buffer,p+32,512);
 BSP_SDMMC_CaptureCopyFault(SYS_ERR_PROTOCOL,8);
 CHECK(g_sd_copy_debug.fault.sequence==sequence&&g_sd_copy_debug.fault.source==source);
 CHECK(g_sd_copy_debug.fault.result==(id==3?SYS_ERR_TIMEOUT:SYS_ERR_IO));
 CHECK(__get_PRIMASK()==0);
 return 0;
}
