//复用08A真实识卡桩及原断言，08B不修改历史用例
#include "../SdmmcStage08A3/sdmmc_test.c"
#include "../../CM7/BSP/bsp_sdmmc_memory.c"

//模拟硬件事件和卡编程等待，IDMA/HAL事务代码仍使用正式源文件
static uint32_t s_case, s_waits, s_starts, s_notify, s_last_lba, s_last_blocks;
static uint32_t s_stop_error, s_async_error, s_card_busy, s_speed_calls;
static bool s_can_wait = true, s_abort_stuck;

//独立模拟卡介质，用于写测试备份和恢复验证
static uint8_t s_test_medium[4096];

//保存129扇区调用者缓冲区及边界哨兵，位于DTCM而不是IDMA区
static uint8_t s_cpu_buffer[129U * 512U + 32U];

//模拟终止事件的接收与数据搬运结果
static SysResult_t Test_Wait(void *context, uint32_t timeout_ms);

//验证普通任务等待环境
static bool Test_CanWait(void *context);

//记录中断通知，不把通知当成成功结果
static void Test_Notify(void *context);

//模拟命令启动，检查真实HAL已把IDMA地址配置到AXI
static uint32_t Test_Command(SDMMC_TypeDef *sd, uint32_t address, uint32_t blocks);

//模拟MPU配置寄存器，属性检查使用真实MemoryValid实现
void HAL_MPU_Disable(void){MPU->CTRL=0U;}
void HAL_MPU_Enable(uint32_t control){MPU->CTRL=control|MPU_CTRL_ENABLE_Msk;}
void HAL_MPU_ConfigRegion(const MPU_Region_InitTypeDef *v)
{
    MPU->RNR=v->Number;MPU->RBAR=v->BaseAddress;
    MPU->RASR=(v->Enable)|(v->Size<<MPU_RASR_SIZE_Pos)|(v->SubRegionDisable<<8U)|
        (v->IsBufferable<<16U)|(v->IsCacheable<<17U)|(v->IsShareable<<18U)|
        (v->TypeExtField<<19U)|(v->AccessPermission<<24U)|(v->DisableExec<<28U);
}

//速度协商桩只模拟卡接受，不替代HAL读写或IRQ实现
HAL_StatusTypeDef HAL_SD_ConfigWideBusOperation(SD_HandleTypeDef *h,uint32_t width)
{
    if(s_case==25U)return HAL_ERROR;
    h->Instance->CLKCR=(h->Instance->CLKCR&~SDMMC_CLKCR_WIDBUS)|width;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_SD_ConfigSpeedBusOperation(SD_HandleTypeDef *h,uint32_t mode)
{
    (void)h;s_speed_calls++;
    if(mode!=SDMMC_SPEED_MODE_HIGH || s_case==26U)return HAL_ERROR;
    return HAL_OK;
}

//模拟LL寄存器配置，正式HAL负责传输方向、长度及IDMA启停
HAL_StatusTypeDef SDMMC_Init(SDMMC_TypeDef *sd,SDMMC_InitTypeDef init)
{
    sd->CLKCR=init.ClockDiv|init.BusWide|init.ClockEdge|init.HardwareFlowControl;
    return HAL_OK;
}
HAL_StatusTypeDef SDMMC_ConfigData(SDMMC_TypeDef *sd,const SDMMC_DataInitTypeDef *v)
{
    sd->DTIMER=v->DataTimeOut;sd->DLEN=v->DataLength;
    sd->DCTRL=v->TransferDir|v->TransferMode|v->DataBlockSize|v->DPSM;
    return HAL_OK;
}
uint32_t SDMMC_CmdReadSingleBlock(SDMMC_TypeDef *s,uint32_t a){return Test_Command(s,a,1U);}
uint32_t SDMMC_CmdReadMultiBlock(SDMMC_TypeDef *s,uint32_t a){return Test_Command(s,a,s->DLEN/512U);}
uint32_t SDMMC_CmdWriteSingleBlock(SDMMC_TypeDef *s,uint32_t a){return Test_Command(s,a,1U);}
uint32_t SDMMC_CmdWriteMultiBlock(SDMMC_TypeDef *s,uint32_t a){return Test_Command(s,a,s->DLEN/512U);}
uint32_t SDMMC_CmdStopTransfer(SDMMC_TypeDef *sd)
{
    if(s_abort_stuck)return HAL_SD_ERROR_CMD_CRC_FAIL;
    (*(volatile uint32_t *)((uint32_t)sd + offsetof(SDMMC_TypeDef, STA)))&=~(SDMMC_FLAG_DPSMACT|SDMMC_FLAG_CMDACT);
    (*(volatile uint32_t *)((uint32_t)sd + offsetof(SDMMC_TypeDef, STA)))|=SDMMC_FLAG_DABORT|SDMMC_FLAG_BUSYD0END;
    return s_stop_error;
}

//真实HAL驱动命令写入，确认IDMA没有接触DTCM调用者缓冲区
static uint32_t Test_Command(SDMMC_TypeDef *sd,uint32_t address,uint32_t blocks)
{
    s_starts++;s_last_lba=address;s_last_blocks=blocks;
    if(sd->IDMABASE0!=BSP_SDMMC_DMA_BASE || blocks==0U || blocks>64U || sd->IDMACTRL!=SDMMC_ENABLE_IDMA_SINGLE_BUFF)
        return HAL_SD_ERROR_PARAM;
    if(s_case==12U)return HAL_SD_ERROR_CMD_RSP_TIMEOUT;
    (*(volatile uint32_t *)((uint32_t)sd + offsetof(SDMMC_TypeDef, STA)))|=SDMMC_FLAG_DPSMACT;
    return HAL_SD_ERROR_NONE;
}

//任务环境桩
static bool Test_CanWait(void *context){(void)context;return s_can_wait;}

//记录IRQ唤醒
static void Test_Notify(void *context){(void)context;s_notify++;}

//模拟硬件产生DATAEND或错误，HAL数据处理仍实际执行
static SysResult_t Test_Wait(void *context,uint32_t timeout_ms)
{
    uint32_t i;
    (void)context;s_waits++;s_tick+=timeout_ms;
    if(s_case==11U || s_case==14U || s_case==31U || s_case==38U)
    {
        if(s_case==38U)HAL_SD_RxCpltCallback(&s_sd);
        return SYS_ERR_TIMEOUT;
    }
    if(s_case==30U && s_starts>=2U){s_tick+=300U;return SYS_ERR_TIMEOUT;}
    if(s_case==34U)return SYS_ERR_IO;

    if(s_case==10U){s_detect=0U;return SYS_OK;}
    if(s_case==15U && s_waits==1U)
        s_reenter_ok=BSP_SDMMC_ReadBlocks(1U,1U,s_cpu_buffer,100U)==SYS_ERR_BUSY;
    if(s_batch_done && s_card_busy!=0U)
    {
        s_card_busy--;
        if(s_card_busy==0U)s_card_state=HAL_SD_CARD_TRANSFER;
        return SYS_OK;
    }
    if(s_sd.State!=HAL_SD_STATE_BUSY)return SYS_OK;
    if(s_case>=40U && s_last_lba==100U && s_last_blocks<=8U)
    {
        if(s_batch_write)memcpy(s_test_medium,g_sdmmc_dma_buffer,s_last_blocks*512U);
        else memcpy(g_sdmmc_dma_buffer,s_test_medium,s_last_blocks*512U);
    }
    else if(!s_batch_write)
    {
        for(i=0U;i<s_last_blocks*512U;i++)
            g_sdmmc_dma_buffer[i]=(uint8_t)((s_last_lba*512U+i)^0x5AU);
    }
    else
    {
        for(i=0U;i<s_last_blocks*512U;i++)
            if(g_sdmmc_dma_buffer[i]!=(uint8_t)((s_last_lba*512U+i)^0x5AU))s_bad_clock_request=1U;
    }
    if(s_case==43U)(*(volatile uint32_t *)((uint32_t)SDMMC1+offsetof(SDMMC_TypeDef,DCOUNT)))=4U;
    if(s_case==39U && s_waits==1U){HAL_SD_RxCpltCallback(&s_sd);return SYS_OK;}
    if(s_case==35U)s_state_error=HAL_SD_ERROR_CMD_CRC_FAIL;
    if(s_case==8U)s_async_error=SDMMC_FLAG_DCRCFAIL;
    if(s_case==9U)s_async_error=SDMMC_FLAG_DTIMEOUT;
    if(s_case==13U)s_async_error=SDMMC_FLAG_IDMATE;
    if(s_case==16U)s_async_error=SDMMC_FLAG_RXOVERR;
    if(s_case==17U)s_async_error=SDMMC_FLAG_TXUNDERR;
    (*(volatile uint32_t *)((uint32_t)SDMMC1 + offsetof(SDMMC_TypeDef, STA)))&=~SDMMC_FLAG_DPSMACT;
    (*(volatile uint32_t *)((uint32_t)SDMMC1 + offsetof(SDMMC_TypeDef, STA)))|=SDMMC_FLAG_DATAEND|s_async_error;
    if(s_case==27U && s_starts==1U)s_stop_error=HAL_SD_ERROR_CMD_CRC_FAIL;
    if(s_case!=7U && s_case!=13U)SDMMC1_IRQHandler();
    return SYS_OK;
}


//未执行的厂商功能仅为链接提供桩，意外进入必须使测试失败
static uint32_t s_unexpected_calls;
uint32_t SDMMC_CmdAppCommand(SDMMC_TypeDef *SDMMCx, uint32_t Argument){(void)SDMMCx;(void)Argument;s_unexpected_calls++;return 0U;}
uint32_t SDMMC_CmdAppOperCommand(SDMMC_TypeDef *SDMMCx, uint32_t Argument){(void)SDMMCx;(void)Argument;s_unexpected_calls++;return 0U;}
uint32_t SDMMC_CmdBlockLength(SDMMC_TypeDef *SDMMCx, uint32_t BlockSize){(void)SDMMCx;(void)BlockSize;s_unexpected_calls++;return 0U;}
uint32_t SDMMC_CmdBusWidth(SDMMC_TypeDef *SDMMCx, uint32_t BusWidth){(void)SDMMCx;(void)BusWidth;s_unexpected_calls++;return 0U;}
uint32_t SDMMC_CmdErase(SDMMC_TypeDef *SDMMCx, uint32_t EraseType){(void)SDMMCx;(void)EraseType;s_unexpected_calls++;return 0U;}
uint32_t SDMMC_CmdGoIdleState(SDMMC_TypeDef *SDMMCx){(void)SDMMCx;s_unexpected_calls++;return 0U;}
uint32_t SDMMC_CmdOperCond(SDMMC_TypeDef *SDMMCx){(void)SDMMCx;s_unexpected_calls++;return 0U;}
uint32_t SDMMC_CmdSDEraseEndAdd(SDMMC_TypeDef *SDMMCx, uint32_t EndAdd){(void)SDMMCx;(void)EndAdd;s_unexpected_calls++;return 0U;}
uint32_t SDMMC_CmdSDEraseStartAdd(SDMMC_TypeDef *SDMMCx, uint32_t StartAdd){(void)SDMMCx;(void)StartAdd;s_unexpected_calls++;return 0U;}
uint32_t SDMMC_CmdSelDesel(SDMMC_TypeDef *SDMMCx, uint32_t Addr){(void)SDMMCx;(void)Addr;s_unexpected_calls++;return 0U;}
uint32_t SDMMC_CmdSendCID(SDMMC_TypeDef *SDMMCx){(void)SDMMCx;s_unexpected_calls++;return 0U;}
uint32_t SDMMC_CmdSendCSD(SDMMC_TypeDef *SDMMCx, uint32_t Argument){(void)SDMMCx;(void)Argument;s_unexpected_calls++;return 0U;}
uint32_t SDMMC_CmdSendSCR(SDMMC_TypeDef *SDMMCx){(void)SDMMCx;s_unexpected_calls++;return 0U;}
uint32_t SDMMC_CmdSendStatus(SDMMC_TypeDef *SDMMCx, uint32_t Argument){(void)SDMMCx;(void)Argument;s_unexpected_calls++;return 0U;}
uint32_t SDMMC_CmdSetRelAdd(SDMMC_TypeDef *SDMMCx, uint16_t *pRCA){(void)SDMMCx;(void)pRCA;s_unexpected_calls++;return 0U;}
uint32_t SDMMC_CmdStatusRegister(SDMMC_TypeDef *SDMMCx){(void)SDMMCx;s_unexpected_calls++;return 0U;}
uint32_t SDMMC_CmdSwitch(SDMMC_TypeDef *SDMMCx, uint32_t Argument){(void)SDMMCx;(void)Argument;s_unexpected_calls++;return 0U;}
uint32_t SDMMC_GetPowerState(const SDMMC_TypeDef *SDMMCx){(void)SDMMCx;s_unexpected_calls++;return 0U;}
uint32_t SDMMC_GetResponse(const SDMMC_TypeDef *SDMMCx, uint32_t Response){(void)SDMMCx;(void)Response;s_unexpected_calls++;return 0U;}
HAL_StatusTypeDef SDMMC_PowerState_OFF(SDMMC_TypeDef *SDMMCx){(void)SDMMCx;s_unexpected_calls++;return HAL_ERROR;}
HAL_StatusTypeDef SDMMC_PowerState_ON(SDMMC_TypeDef *SDMMCx){(void)SDMMCx;s_unexpected_calls++;return HAL_ERROR;}
uint32_t SDMMC_ReadFIFO(const SDMMC_TypeDef *SDMMCx){(void)SDMMCx;s_unexpected_calls++;return 0U;}
HAL_StatusTypeDef SDMMC_WriteFIFO(SDMMC_TypeDef *SDMMCx, uint32_t *pWriteData){(void)SDMMCx;(void)pWriteData;s_unexpected_calls++;return HAL_ERROR;}

//NVIC调用只模拟硬件配置，不决定传输成功结果
void HAL_NVIC_SetPriority(IRQn_Type irq,uint32_t p,uint32_t sub){(void)sub;NVIC_SetPriority(irq,p);}
void HAL_NVIC_EnableIRQ(IRQn_Type irq){NVIC->ISER[(uint32_t)irq>>5U]|=1UL<<((uint32_t)irq&31U);}
void HAL_NVIC_DisableIRQ(IRQn_Type irq){NVIC_DisableIRQ(irq);}
void HAL_NVIC_ClearPendingIRQ(IRQn_Type irq){NVIC_ClearPendingIRQ(irq);}
void HAL_Delay(uint32_t ms){s_tick+=ms;}

//执行单个08B验收场景
uint32_t Sdmmc08B_TestRun(uint32_t id)
{
    BspSdmmcWaitOps_t ops;
    BspSdmmcIoDiagnostics_t d;
    uint32_t i,count=1U,timeout=200U,lba=16U;
    SysResult_t result,expected=SYS_OK;
    s_case=id;Setup();
    s_sd.SdCard.CardSpeed=CARD_HIGH_SPEED;
    CHECK(BSP_SDMMC_MemoryInit()==SYS_OK);
    CHECK(BSP_SDMMC_MemoryValid());
    ops.can_wait=Test_CanWait;ops.wait=Test_Wait;ops.notify=Test_Notify;ops.context=NULL;
    CHECK(BSP_SDMMC_SetWaitOps(&ops)==SYS_OK);
    CHECK(BSP_SDMMC_Init()==SYS_OK);
    s_sd.SdCard.CardType=CARD_SDHC_SDXC;
    s_sd.SdCard.LogBlockNbr=s_card.logical_block_count;
    s_sd.SdCard.CardSpeed=id==24U?CARD_NORMAL_SPEED:CARD_HIGH_SPEED;
    result=BSP_SDMMC_EnableTransferMode();
    if(id==25U || id==26U)
    {
        CHECK(result==SYS_ERR_IO);CHECK(s_io.fault==1U && s_io.transfer_ready==0U);return 0U;
    }
    CHECK(result==SYS_OK);
    CHECK((SDMMC1->CLKCR&SDMMC_CLKCR_CLKDIV)==(id==24U?5U:3U));
    CHECK(s_diagnostics.bus_width==4U && s_diagnostics.sd_clock_hz==(id==24U?24000000U:40000000U));
    CHECK(NVIC_GetPriority(SDMMC1_IRQn)==7U);
    for(i=0U;i<sizeof(s_cpu_buffer);i++)s_cpu_buffer[i]=0xC7U;
    if(id==1U || id==3U || id==27U)count=64U;
    if(id==2U || id==4U || id==30U)count=129U;
    if(id==41U)lba=s_card.logical_block_count-1U;
    if(id==5U)count=0U;
    if(id==6U)lba=s_card.logical_block_count;
    if(id==8U)expected=SYS_ERR_IO;
    if(id==9U || id==11U || id==12U || id==14U || id==30U || id==31U || id==38U)expected=SYS_ERR_TIMEOUT;
    if(id==10U)expected=SYS_ERR_NOT_FOUND;
    if(id==13U || id==16U || id==17U || id==27U || id==34U || id==35U)expected=SYS_ERR_IO;
    if(id==5U || id==6U)expected=SYS_ERR_INVALID_ARG;
    if(id==14U || id==31U)s_abort_stuck=true;
    if(id==18U){s_card_busy=3U;s_card_state=HAL_SD_CARD_PROGRAMMING;}
    if(id==19U){s_can_wait=false;expected=SYS_ERR_INVALID_STATE;}
    if(id==20U){MPU->RASR|=MPU_RASR_C_Msk;expected=SYS_ERR_INVALID_STATE;}
    if(id==21U || id==36U)SCB->CCR|=SCB_CCR_DC_Msk;
    if(id==36U)CHECK(BSP_SDMMC_MemoryInit()==SYS_ERR_INVALID_STATE);
    if(id==37U){MPU->CTRL=0U;expected=SYS_ERR_INVALID_STATE;}
    if(id==40U){SDMMC1->CLKCR=5U|SDMMC_BUS_WIDE_4B;expected=SYS_ERR_INVALID_STATE;}
    if(id==43U)expected=SYS_ERR_PROTOCOL;
    if(id==32U)
    {
        CHECK(BSP_SDMMC_ReadBlocks(UINT32_MAX-1U,4U,s_cpu_buffer,200U)==SYS_ERR_INVALID_ARG);
        CHECK(s_starts==0U);return 0U;
    }
    if(id==33U)
    {
        CHECK(BSP_SDMMC_DeInit()==SYS_OK);CHECK(BSP_SDMMC_Init()==SYS_OK);
        s_sd.SdCard.CardSpeed=CARD_NORMAL_SPEED;
        CHECK(BSP_SDMMC_EnableTransferMode()==SYS_OK);
        CHECK(s_io.high_speed==0U && s_diagnostics.sd_clock_hz==24000000U);
        s_sd.SdCard.CardType=CARD_SDHC_SDXC;s_sd.SdCard.LogBlockNbr=s_card.logical_block_count;
    }

    if(id==22U){s_tick=0xffffffe0U;timeout=200U;}
    if(id==28U)
    {
        CHECK(BSP_SDMMC_ReadBlocks(lba,1U,(void *)BSP_SDMMC_DMA_BASE,200U)==SYS_ERR_INVALID_ARG);
        CHECK(BSP_SDMMC_ReadBlocks(lba,1U,(void *)0x40000000UL,200U)==SYS_ERR_INVALID_ARG);
        CHECK(BSP_SDMMC_ReadBlocks(lba,UINT32_MAX,s_cpu_buffer,200U)==SYS_ERR_INVALID_ARG);
        CHECK(BSP_SDMMC_ReadBlocks(lba,1U,NULL,200U)==SYS_ERR_INVALID_ARG);
        CHECK(BSP_SDMMC_ReadBlocks(lba,1U,s_cpu_buffer,0U)==SYS_ERR_INVALID_ARG);
        CHECK(s_starts==0U);return 0U;
    }
    if(id==42U)
    {
        CHECK(BSP_SDMMC_ReadBlocks(lba,1U,s_cpu_buffer+1U,200U)==SYS_OK);
        CHECK(s_cpu_buffer[0]==0xC7U && s_cpu_buffer[513]==0xC7U);
        for(i=0U;i<512U;i++)CHECK(s_cpu_buffer[i+1U]==(uint8_t)((lba*512U+i)^0x5AU));
        return 0U;
    }
    if(id==29U)
    {
        CHECK(BSP_SDMMC_ReadBlocks(lba,1U,(void *)0xffffff00UL,200U)==SYS_ERR_INVALID_ARG);
        CHECK(BSP_SDMMC_ReadBlocks(lba,1U,(void *)0x2001ff00UL,200U)==SYS_ERR_INVALID_ARG);
        CHECK(s_starts==0U);return 0U;
    }
    if(id==3U || id==4U || id==17U || id==18U)
        for(i=0U;i<count*512U;i++)s_cpu_buffer[i]=(uint8_t)((lba*512U+i)^0x5AU);
    result=(id==3U || id==4U || id==17U || id==18U)?BSP_SDMMC_WriteBlocks(lba,count,s_cpu_buffer,timeout):
        BSP_SDMMC_ReadBlocks(lba,count,s_cpu_buffer,timeout);
    CHECK(result==expected);
    CHECK(BSP_SDMMC_GetIoDiagnostics(&d)==SYS_OK);
    if(result==SYS_OK)
    {
        CHECK(d.active==0U && d.completed_blocks==count && d.result==SYS_OK);
        CHECK(s_starts==(count+63U)/64U);
        CHECK(s_cpu_buffer[count*512U]==0xC7U);
        if(id!=3U && id!=4U && id!=18U)
            for(i=0U;i<count*512U;i++)CHECK(s_cpu_buffer[i]==(uint8_t)((lba*512U+i)^0x5AU));
        CHECK(s_bad_clock_request==0U);
        if(id==7U)CHECK(d.lost_irq_polls!=0U && s_notify==0U);
        if(id==15U)CHECK(s_reenter_ok);
        if(id==39U)CHECK(s_waits>=2U);
        if(id==18U)CHECK(s_card_busy==0U && s_waits>=4U);
        if(id==23U)
        {
            HAL_SD_RxCpltCallback(&s_sd);CHECK(!s_batch_active);
            CHECK(BSP_SDMMC_ReadBlocks(lba,1U,s_cpu_buffer,timeout)==SYS_OK);
        }
    }
    else if((id>=8U && id<=17U && id!=15U) || id==27U || id==30U || id==31U || id==34U || id==35U || id==38U || id==43U)
    {
        CHECK(d.fault==1U && d.result==expected);
        CHECK(BSP_SDMMC_ReadBlocks(lba,1U,s_cpu_buffer,200U)==SYS_ERR_INVALID_STATE);
        if(id==30U)CHECK(d.completed_blocks==64U && s_cpu_buffer[64U*512U]==0xC7U);
        if(id==8U || id==9U || id==13U || id==35U)CHECK(d.completed_blocks==0U && s_cpu_buffer[0]==0xC7U);
        if(id==31U)CHECK(BSP_SDMMC_Recover()==SYS_ERR_IO);
        if(id==14U || id==31U)
            CHECK(d.active==1U && d.recovery_result!=SYS_OK);
        s_abort_stuck=false;s_stop_error=0U;
        CHECK(BSP_SDMMC_Recover()==SYS_OK);
        CHECK(s_io.active==0U && s_io.fault==0U && s_io.transfer_ready==0U);
        CHECK(s_diagnostics.ready==0U);
        CHECK(BSP_SDMMC_ReadBlocks(lba,1U,s_cpu_buffer,200U)==SYS_ERR_INVALID_STATE);
    }
    CHECK(s_unexpected_calls==0U);
    CHECK(s_unexpected_calls==0U);
    return 0U;
}
