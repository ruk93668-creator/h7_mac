//直接编译真实BSP与实板入口，仅替换HAL函数；寄存器访问仍使用真实CMSIS定义
#include "../../CM7/BSP/bsp_sdmmc.c"
#include "sdmmc_probe.c"

//记录首个失败行号和本用例断言数量
volatile struct {uint32_t checks, line;} g_test_result;

//保存本次可控HAL行为和调用证据
static uint32_t s_tick, s_detect, s_pll=240000000U, s_kernel=240000000U;
static HAL_StatusTypeDef s_clock_status, s_init_status, s_info_status, s_deinit_status;
static uint32_t s_init_error, s_state_error, s_card_state=HAL_SD_CARD_TRANSFER;
static uint32_t s_calls, s_clock_calls, s_deinits, s_gpio_calls, s_gpio_c, s_gpio_d;
static uint32_t s_corrupt, s_reenter, s_reenter_ok, s_bad_clock_request;
static HAL_SD_CardInfoTypeDef s_mock_info;

//独立仿真不依赖C运行库
void *memset(void *d,int c,size_t n){uint8_t *p=d;while(n--)*p++=(uint8_t)c;return d;}
void *memcpy(void *d,const void *s,size_t n){uint8_t *a=d;const uint8_t *b=s;while(n--)*a++=*b++;return d;}
void __aeabi_memcpy(void *d,const void *s,size_t n){volatile uint8_t *a=d;const volatile uint8_t *b=s;while(n--)*a++=*b++;}
void __aeabi_memcpy4(void *d,const void *s,size_t n){__aeabi_memcpy(d,s,n);}
void __aeabi_memclr(void *d,size_t n){volatile uint8_t *a=d;while(n--)*a++=0;}
void __aeabi_memclr4(void *d,size_t n){__aeabi_memclr(d,n);}
void __aeabi_memclr8(void *d,size_t n){__aeabi_memclr(d,n);}

//Mock时间只由测试推进，HAL内部真实硬件超时不在此伪造验证
uint32_t HAL_GetTick(void){return s_tick++;}
void HAL_RCCEx_GetPLL1ClockFreq(PLL1_ClocksTypeDef *c){memset(c,0,sizeof(*c));c->PLL1_Q_Frequency=s_pll;}
uint32_t HAL_RCCEx_GetPeriphCLKFreq(uint64_t p){(void)p;return s_kernel;}

//仅允许选择SDMMC的PLL1Q，修改PLL参数会被断言捕获
HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *c)
{
    s_clock_calls++;
    if(c->PeriphClockSelection!=RCC_PERIPHCLK_SDMMC || c->SdmmcClockSelection!=RCC_SDMMCCLKSOURCE_PLL || c->PLL2.PLL2M!=0U)s_bad_clock_request=1;
    if(s_clock_status==HAL_OK && s_corrupt!=5U)__HAL_RCC_SDMMC_CONFIG(c->SdmmcClockSelection);
    return s_clock_status;
}

//记录GPIO掩码，确保DAT1到DAT3及其他外设引脚未被配置
void HAL_GPIO_Init(GPIO_TypeDef *port,const GPIO_InitTypeDef *g)
{
    s_gpio_calls++;
    if(port==GPIOC){if(g->Mode!=GPIO_MODE_AF_PP || g->Alternate!=GPIO_AF12_SDMMC1 || g->Pull!=GPIO_NOPULL)s_bad_clock_request=1;s_gpio_c|=g->Pin;}
    if(port==GPIOD)s_gpio_d|=g->Pin;
}
void HAL_GPIO_DeInit(GPIO_TypeDef *port,uint32_t pins){if(port==GPIOC)s_gpio_c&=~pins;if(port==GPIOD)s_gpio_d&=~pins;}
GPIO_PinState HAL_GPIO_ReadPin(const GPIO_TypeDef *port,uint16_t pins){(void)port;(void)pins;return s_detect?GPIO_PIN_SET:GPIO_PIN_RESET;}

//模拟HAL会调用MSP，并可在初始化内部尝试重入BSP
HAL_StatusTypeDef HAL_SD_Init(SD_HandleTypeDef *h)
{
    BspSdmmcCardInfo_t info;
    BspSdmmcDiagnostics_t d;
    s_calls++;HAL_SD_MspInit(h);s_tick+=20U;
    if(s_reenter)
        s_reenter_ok=BSP_SDMMC_Init()==SYS_ERR_BUSY && BSP_SDMMC_GetCardInfo(&info)==SYS_ERR_BUSY && BSP_SDMMC_GetDiagnostics(&d)==SYS_ERR_BUSY && BSP_SDMMC_DeInit()==SYS_ERR_BUSY;
    h->State=HAL_SD_STATE_READY;h->ErrorCode=s_init_error;
    SDMMC1->CLKCR=h->Init.ClockDiv;
    if(s_corrupt==1U)SDMMC1->CLKCR=0U;
    if(s_corrupt==2U)SDMMC1->CLKCR|=SDMMC_BUS_WIDE_4B;
    if(s_corrupt==3U)SDMMC1->IDMACTRL=1U;
    if(s_corrupt==4U)SDMMC1->MASK=1U;
    return s_init_status;
}

//清理成功才归还GPIO，失败保留硬件占用供重试
HAL_StatusTypeDef HAL_SD_DeInit(SD_HandleTypeDef *h)
{
    s_deinits++;
    if(s_deinit_status==HAL_OK){HAL_SD_MspDeInit(h);h->ErrorCode=0;h->State=HAL_SD_STATE_RESET;}
    return s_deinit_status;
}
HAL_StatusTypeDef HAL_SD_GetCardInfo(const SD_HandleTypeDef *h,HAL_SD_CardInfoTypeDef *info){(void)h;*info=s_mock_info;return s_info_status;}
HAL_SD_CardStateTypeDef HAL_SD_GetCardState(SD_HandleTypeDef *h){h->ErrorCode=s_state_error;return s_card_state;}
HAL_SD_StateTypeDef HAL_SD_GetState(const SD_HandleTypeDef *h){return h->State;}
uint32_t HAL_SD_GetError(const SD_HandleTypeDef *h){return h->ErrorCode;}

//断言失败时返回C行号，Python判定每个真实执行结果
#define CHECK(x) do{g_test_result.checks++;if(!(x)){g_test_result.line=__LINE__;return 1U;}}while(0)

//建立TIM6正常工作的上下文及可被验证的PLL寄存器哨兵
static void Setup(void)
{
    s_detect=BSP_SDMMC_DETECT_ACTIVE_LOW?0U:1U;
    TIM6->CR1=TIM_CR1_CEN;TIM6->DIER=TIM_DIER_UIE;
    NVIC->ISER[(uint32_t)TIM6_DAC_IRQn>>5U]=1UL<<((uint32_t)TIM6_DAC_IRQn&31U);
    NVIC_SetPriority(TIM6_DAC_IRQn,4U);
    RCC->CR|=RCC_CR_PLL1RDY;RCC->PLL1DIVR=0x12345678U;RCC->PLL2DIVR=0x3456789AU;
    __HAL_RCC_SDMMC_CONFIG(RCC_SDMMCCLKSOURCE_PLL2);
    s_mock_info.CardType=CARD_SDHC_SDXC;s_mock_info.CardVersion=2U;
    s_mock_info.LogBlockNbr=0x4000000U;s_mock_info.LogBlockSize=512U;
    s_mock_info.BlockNbr=s_mock_info.LogBlockNbr;s_mock_info.BlockSize=512U;
}

//配置、识卡、诊断与资源所有权用例
static uint32_t Case(uint32_t id)
{
    BspSdmmcCardInfo_t info;
    BspSdmmcDiagnostics_t diag;
    uint32_t raw=9U;
    bool present=false;
    SysResult_t expected=SYS_OK,result;
    Setup();
    if(id==1U){s_detect=BSP_SDMMC_DETECT_ACTIVE_LOW?1U:0U;expected=SYS_ERR_NOT_FOUND;}
    if(id==2U){s_pll=120000000U;expected=SYS_ERR_INVALID_STATE;}
    if(id==3U){s_clock_status=HAL_ERROR;expected=SYS_ERR_IO;}
    if(id==4U){s_clock_status=HAL_TIMEOUT;expected=SYS_ERR_TIMEOUT;}
    if(id==5U){s_kernel=0U;expected=SYS_ERR_INVALID_STATE;}
    if(id==6U){s_corrupt=5U;expected=SYS_ERR_INVALID_STATE;}
    if(id==7U){s_init_status=HAL_ERROR;s_init_error=HAL_SD_ERROR_CMD_CRC_FAIL;expected=SYS_ERR_IO;}
    if(id==8U){s_init_status=HAL_ERROR;s_init_error=HAL_SD_ERROR_CMD_RSP_TIMEOUT;expected=SYS_ERR_TIMEOUT;}
    if(id==9U){s_init_status=HAL_TIMEOUT;expected=SYS_ERR_TIMEOUT;}
    if(id==10U){s_init_status=HAL_BUSY;expected=SYS_ERR_BUSY;}
    if(id==11U){s_info_status=HAL_ERROR;expected=SYS_ERR_IO;}
    if(id==12U){s_info_status=HAL_TIMEOUT;expected=SYS_ERR_TIMEOUT;}
    if(id==13U){s_mock_info.LogBlockNbr=0U;expected=SYS_ERR_PROTOCOL;}
    if(id==14U){s_mock_info.LogBlockSize=1024U;expected=SYS_ERR_PROTOCOL;}
    if(id==15U){s_mock_info.CardType=5U;expected=SYS_ERR_PROTOCOL;}
    if(id==16U){s_card_state=HAL_SD_CARD_PROGRAMMING;expected=SYS_ERR_INVALID_STATE;}
    if(id==17U){s_state_error=HAL_SD_ERROR_TIMEOUT;expected=SYS_ERR_TIMEOUT;}
    if(id==18U){s_state_error=HAL_SD_ERROR_CMD_CRC_FAIL;expected=SYS_ERR_IO;}
    if(id>=19U && id<=22U){s_corrupt=id-18U;expected=SYS_ERR_PROTOCOL;}
    if(id==23U){s_init_status=HAL_ERROR;s_init_error=HAL_SD_ERROR_CMD_CRC_FAIL;s_deinit_status=HAL_ERROR;expected=SYS_ERR_IO;}
    if(id==24U)s_reenter=1U;
    if(id==26U){s_init_status=HAL_ERROR;expected=SYS_ERR_IO;}
    if(id==28U){CHECK(BSP_SDMMC_GetCardInfo(NULL)==SYS_ERR_INVALID_ARG);CHECK(BSP_SDMMC_GetDiagnostics(NULL)==SYS_ERR_INVALID_ARG);CHECK(BSP_SDMMC_ReadDetect(NULL,&present)==SYS_ERR_INVALID_ARG);CHECK(BSP_SDMMC_ReadDetect(&raw,NULL)==SYS_ERR_INVALID_ARG);CHECK(s_calls==0);return 0;}
    if(id==29U){info.card_type=0xAABBCCDDU;CHECK(BSP_SDMMC_GetCardInfo(&info)==SYS_ERR_INVALID_STATE);CHECK(info.card_type==0xAABBCCDDU);return 0;}
    if(id==30U || id==31U || (id>=34U && id<=36U)){
        if(id==31U)__set_PRIMASK(1U);
        if(id==34U)TIM6->CR1=0U;
        if(id==35U)TIM6->DIER=0U;
        if(id==36U)NVIC->ISER[(uint32_t)TIM6_DAC_IRQn>>5U]=0U;
        CHECK(BSP_SDMMC_Init()==SYS_ERR_INVALID_STATE);CHECK(s_calls==0U);return 0;
    }
    if(id==32U){__set_BASEPRI(64U);CHECK(BSP_SDMMC_Init()==SYS_ERR_INVALID_STATE);return 0;}
    if(id==33U)__set_BASEPRI(80U);
    if(id==37U){SD_HandleTypeDef other={0};other.Instance=SDMMC1;HAL_SD_MspInit(&other);HAL_SD_MspDeInit(&other);CHECK(s_gpio_calls==0 && s_gpio_c==0 && s_gpio_d==0);return 0;}
    if(id==39U){CHECK(BSP_SDMMC_ReadDetect(&raw,&present)==SYS_OK);CHECK(raw==(BSP_SDMMC_DETECT_ACTIVE_LOW?0U:1U) && present);CHECK(s_calls==0 && s_clock_calls==0);s_detect=BSP_SDMMC_DETECT_ACTIVE_LOW?1U:0U;CHECK(BSP_SDMMC_ReadDetect(&raw,&present)==SYS_OK);CHECK(raw==(BSP_SDMMC_DETECT_ACTIVE_LOW?1U:0U) && !present);return 0;}
    if(id>=40U && id<=45U){
        if(id==43U){s_init_status=HAL_ERROR;s_init_error=HAL_SD_ERROR_CMD_CRC_FAIL;}
        if(id==44U)s_detect=BSP_SDMMC_DETECT_ACTIVE_LOW?1U:0U;
        CHECK(SdmmcTest_Run()==SYS_OK);
#if APP_SDMMC_TEST_ENABLED
        CHECK(g_sdmmc_test.enabled==1U && g_sdmmc_test.finished==1U);
#if APP_SDMMC_DETECT_ONLY
        CHECK(s_calls==0 && s_clock_calls==0 && g_sdmmc_test.result==SYS_OK);
#else
        CHECK(g_sdmmc_test.result==(id==43U?SYS_ERR_IO:id==44U?SYS_ERR_NOT_FOUND:SYS_OK));
        if(id!=43U && id!=44U)CHECK(g_sdmmc_test.card.capacity_bytes==UINT64_C(34359738368));
#endif
        result=(SysResult_t)s_calls;CHECK(SdmmcTest_Run()==SYS_OK);CHECK(s_calls==(uint32_t)result);
#else
        CHECK(g_sdmmc_test.attempted==0 && s_calls==0 && s_gpio_calls==0);
#endif
        return 0;
    }
    if(id==46U || id==47U){RCC->AHB2ENR|=RCC_AHB2ENR_SDMMC2EN;if(id==46U)expected=SYS_ERR_BUSY;else __HAL_RCC_SDMMC_CONFIG(RCC_SDMMCCLKSOURCE_PLL);}
    if(id==48U)s_tick=0xfffffff0U;
    if(id==49U){RCC->CR&=~RCC_CR_PLL1RDY;expected=SYS_ERR_INVALID_STATE;}
    result=BSP_SDMMC_Init();CHECK(result==expected);CHECK(s_busy==false);
    CHECK(BSP_SDMMC_GetDiagnostics(&diag)==SYS_OK);CHECK(diag.init_result==expected);
    CHECK(RCC->PLL1DIVR==0x12345678U && RCC->PLL2DIVR==0x3456789AU);CHECK(!s_bad_clock_request);
    if(expected==SYS_OK){
        CHECK(BSP_SDMMC_GetCardInfo(&info)==SYS_OK);CHECK(info.capacity_bytes==UINT64_C(34359738368));
        CHECK(diag.ready==1 && diag.clock_div==300 && diag.sd_clock_hz==400000 && diag.bus_width==1);
        CHECK(s_gpio_c==(GPIO_PIN_8|GPIO_PIN_12) && s_gpio_d==GPIO_PIN_2);
        if(id==24U)CHECK(s_reenter_ok);
        if(id==25U){CHECK(BSP_SDMMC_Init()==SYS_ERR_ALREADY_EXISTS);CHECK(s_calls==1);}
        if(id==27U){CHECK(BSP_SDMMC_DeInit()==SYS_OK);CHECK(s_gpio_c==0 && s_gpio_d==0);CHECK(BSP_SDMMC_GetCardInfo(&info)==SYS_ERR_INVALID_STATE);}
        if(id==33U)CHECK(__get_BASEPRI()==80U && __get_PRIMASK()==0U);
        if(id==38U){s_detect=BSP_SDMMC_DETECT_ACTIVE_LOW?1U:0U;info.card_type=123;CHECK(BSP_SDMMC_GetCardInfo(&info)==SYS_ERR_NOT_FOUND);CHECK(info.card_type==123);}
        if(id==48U)CHECK(diag.elapsed_ms>=20U && diag.elapsed_ms<100U);
    }else{
        info.card_type=77;CHECK(BSP_SDMMC_GetCardInfo(&info)==SYS_ERR_INVALID_STATE);CHECK(info.card_type==77);
        CHECK(diag.ready==0);
        if(id==3U)CHECK(diag.rcc_status==HAL_ERROR);
        if(id==1U)CHECK(s_calls==0 && s_clock_calls==0);
        if(id==8U)CHECK(diag.hal_error==HAL_SD_ERROR_CMD_RSP_TIMEOUT && s_sd.ErrorCode==0);
        if(id!=23U)CHECK(!s_hw_ready && s_gpio_c==0 && s_gpio_d==0);
        if(id==23U){CHECK(diag.cleanup_result==SYS_ERR_IO && s_hw_ready);CHECK(BSP_SDMMC_Init()==SYS_ERR_INVALID_STATE);CHECK(BSP_SDMMC_DeInit()==SYS_ERR_IO);s_deinit_status=HAL_OK;CHECK(BSP_SDMMC_DeInit()==SYS_OK);s_init_status=HAL_OK;s_init_error=0;CHECK(BSP_SDMMC_Init()==SYS_OK);}
        if(id==26U){s_init_status=HAL_OK;CHECK(BSP_SDMMC_Init()==SYS_OK);CHECK(s_calls==2);}
    }
    return 0;
}

//每个用例重新加载ELF，静态状态不会沿用到下一例
uint32_t Sdmmc_TestRun(uint32_t id){return Case(id);}
