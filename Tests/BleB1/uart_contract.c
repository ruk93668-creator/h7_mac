#include "bsp_bt_uart.h"
#include "stm32h7xx_hal.h"
#include <stddef.h>

void USART2_IRQHandler(void);
static uint32_t s_hz = 120000000U;
uint32_t HAL_RCCEx_GetPeriphCLKFreq(uint64_t clock) { (void)clock; return s_hz; }
uint32_t HAL_GetTick(void) { return 0U; }
void HAL_NVIC_SetPriority(IRQn_Type irq, uint32_t pre, uint32_t sub) { (void)sub; NVIC_SetPriority(irq, pre); }
void HAL_NVIC_EnableIRQ(IRQn_Type irq) { NVIC_EnableIRQ(irq); }
void HAL_NVIC_DisableIRQ(IRQn_Type irq) { NVIC_DisableIRQ(irq); }
void HAL_NVIC_ClearPendingIRQ(IRQn_Type irq) { NVIC_ClearPendingIRQ(irq); }
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static BspBtUartDiagnostics_t d;
static uint8_t buffer[64];
static void Inject(uint8_t byte, uint32_t error)
{
    USART2->RDR = byte;
    USART2->ISR |= USART_ISR_RXNE_RXFNE | error;
    USART2_IRQHandler();
}
uint32_t B1_TestRun(uint32_t id)
{
    uint32_t i,j,n,total,old_mode,old_af,old_pull;
    SysResult_t result;
    if (id == 10U)
    {
        CHECK(BSP_BT_UART_Init() == SYS_ERR_TIMEOUT);
        CHECK(BSP_BT_UART_GetDiagnostics(&d) == SYS_OK && d.ready == 0U);
        CHECK((RCC->APB1LENR & RCC_APB1LENR_USART2EN) == 0U);
        CHECK((USART2->CR1 & USART_CR1_UE) == 0U);
        return 0U;
    }
    if (id == 11U)
    {
        s_hz=0U;
        CHECK(BSP_BT_UART_Init() == SYS_ERR_NOT_SUPPORTED);
        CHECK((RCC->APB1LENR & RCC_APB1LENR_USART2EN) == 0U);
        return 0U;
    }
    if (id == 13U)
    {
        RCC->APB1LENR |= RCC_APB1LENR_USART2EN;
        USART2->CR1=USART_CR1_UE | USART_CR1_TE;
        CHECK(BSP_BT_UART_Init() == SYS_ERR_BUSY);
        CHECK(USART2->CR1 == (USART_CR1_UE | USART_CR1_TE));
        CHECK((RCC->APB1LENR & RCC_APB1LENR_USART2EN) != 0U);
        return 0U;
    }
    if (id == 15U)
    {
        __disable_irq();
        result=BSP_BT_UART_Init();
        CHECK(result == SYS_ERR_INVALID_STATE && __get_PRIMASK() == 1U);
        __enable_irq();
        return 0U;
    }
    //预置其他GPIO字段，验证初始化和释放均局部修改
    GPIOD->MODER=0xa5a5a5a5U; GPIOD->AFR[0]=0x12345678U; GPIOA->PUPDR=0x87654321U;
    old_mode=GPIOD->MODER; old_af=GPIOD->AFR[0]; old_pull=GPIOA->PUPDR;
    CHECK(BSP_BT_UART_Init() == SYS_OK);
    CHECK(BSP_BT_UART_GetDiagnostics(&d) == SYS_OK && d.ready == 1U);
    CHECK(d.kernel_hz == 120000000U && d.brr == 1042U);
    CHECK((USART2->CR1 & (USART_CR1_M0|USART_CR1_PCE|USART_CR1_PS|USART_CR1_M1)) == (USART_CR1_M0|USART_CR1_PCE));
    CHECK((USART2->CR3 & (USART_CR3_RTSE|USART_CR3_CTSE|USART_CR3_DMAR|USART_CR3_DMAT)) == 0U);
    CHECK(((GPIOD->AFR[0] >> 20U)&15U) == 7U && ((GPIOA->AFR[0]>>12U)&15U) == 7U);
    CHECK((GPIOD->MODER & ~(3UL<<10U)) == (old_mode & ~(3UL<<10U)));
    if (id == 0U) return 0U;
    if (id == 1U)
    {
        for (j=0U;j<3U;j++)
        {
            for (i=0U;i<BSP_BT_UART_RX_BYTES;i++) Inject((uint8_t)i,0U);
            total=0U;
            while (total<BSP_BT_UART_RX_BYTES)
            {
                CHECK(BSP_BT_UART_Read(buffer,sizeof(buffer),&n) == SYS_OK && n==64U);
                for(i=0U;i<n;i++) CHECK(buffer[i] == (uint8_t)(total+i));
                total+=n;
            }
        }
        CHECK(BSP_BT_UART_Read(buffer,64U,&n) == SYS_ERR_BUSY && n==0U);
        CHECK(BSP_BT_UART_GetDiagnostics(&d)==SYS_OK && d.rx_bytes==6144U && d.overflow==0U);
    }
    else if (id == 2U)
    {
        for(i=0U;i<64U;i++) buffer[i]=(uint8_t)i;
        for(i=0U;i<16U;i++) CHECK(BSP_BT_UART_Write(buffer,64U,&n)==SYS_OK && n==64U);
        CHECK(BSP_BT_UART_Write(buffer,64U,&n)==SYS_ERR_BUSY && n==0U);
        for(i=0U;i<1024U;i++) USART2_IRQHandler();
        CHECK((USART2->CR1 & USART_CR1_TXEIE_TXFNFIE)==0U);
        CHECK(BSP_BT_UART_GetDiagnostics(&d)==SYS_OK && d.tx_pending==0U && d.tx_bytes==1024U && d.tx_idle==0U);
        USART2->ISR |= USART_ISR_TC;
        CHECK(BSP_BT_UART_GetDiagnostics(&d)==SYS_OK && d.tx_idle==1U);
    }
    else if (id == 3U)
    {
        for(i=0U;i<=BSP_BT_UART_RX_BYTES;i++) Inject((uint8_t)i,0U);
        CHECK(BSP_BT_UART_GetDiagnostics(&d)==SYS_OK && d.fault==BSP_BT_UART_ERROR_OVERFLOW && d.overflow==1U);
        CHECK(d.rx_discarded==BSP_BT_UART_RX_BYTES && d.rx_pending==0U);
        CHECK(BSP_BT_UART_Read(buffer,64U,&n)==SYS_ERR_IO && n==0U);
        CHECK(BSP_BT_UART_Recover()==SYS_OK);
        Inject(0x81U,0U);
        CHECK(BSP_BT_UART_Read(buffer,64U,&n)==SYS_OK && n==1U && buffer[0]==0x81U);
        CHECK(BSP_BT_UART_GetDiagnostics(&d)==SYS_OK && d.fault==0U && d.overflow==1U && d.recover_attempts==1U);
    }
    else if (id >= 4U && id <= 8U)
    {
        uint32_t flags= id==4U ? USART_ISR_ORE : id==5U ? USART_ISR_FE : id==6U ? USART_ISR_PE : id==7U ? USART_ISR_NE : 15U;
        buffer[0]=0x55U;
        CHECK(BSP_BT_UART_Write(buffer,1U,&n)==SYS_OK);
        Inject(0x99U,flags);
        CHECK(BSP_BT_UART_GetDiagnostics(&d)==SYS_OK && d.fault!=0U && d.rx_bytes==0U && d.tx_discarded==1U);
        CHECK(d.ore==((flags&USART_ISR_ORE)!=0U) && d.fe==((flags&USART_ISR_FE)!=0U));
        CHECK(d.pe==((flags&USART_ISR_PE)!=0U) && d.ne==((flags&USART_ISR_NE)!=0U));
        CHECK((USART2->CR1 & (USART_CR1_RXNEIE_RXFNEIE | USART_CR1_TXEIE_TXFNFIE | USART_CR1_PEIE))==0U);
        CHECK(BSP_BT_UART_Write(buffer,1U,&n)==SYS_ERR_IO && n==0U);
        CHECK(BSP_BT_UART_Recover()==SYS_OK);
        Inject(0x33U,0U);
        CHECK(BSP_BT_UART_Read(buffer,1U,&n)==SYS_OK && n==1U && buffer[0]==0x33U);
    }
    else if(id==9U)
    {
        GPIOD->MODER ^= 3U;//模拟其他模块修改PD0，不能被释放覆盖
        CHECK(BSP_BT_UART_DeInit()==SYS_OK);
        CHECK(GPIOD->MODER==(old_mode^3U) && GPIOD->AFR[0]==old_af && GPIOA->PUPDR==old_pull);
        CHECK((RCC->APB1LENR & RCC_APB1LENR_USART2EN)==0U);
        CHECK(BSP_BT_UART_DeInit()==SYS_OK);
        CHECK(BSP_BT_UART_Init()==SYS_OK);
    }
    else if(id==12U)
    {
        CHECK(BSP_BT_UART_Init()==SYS_ERR_BUSY);
        CHECK(BSP_BT_UART_Write(NULL,1U,&n)==SYS_ERR_INVALID_ARG && n==0U);
        CHECK(BSP_BT_UART_Read(NULL,1U,&n)==SYS_ERR_INVALID_ARG);
        CHECK(BSP_BT_UART_Write(buffer,1U,NULL)==SYS_ERR_INVALID_ARG);
        CHECK(BSP_BT_UART_GetDiagnostics(NULL)==SYS_ERR_INVALID_ARG);
        CHECK(BSP_BT_UART_Write(NULL,0U,&n)==SYS_OK && n==0U);
        CHECK(BSP_BT_UART_Read(NULL,0U,&n)==SYS_OK && n==0U);
        CHECK(BSP_BT_UART_Read(buffer,64U,&n)==SYS_ERR_BUSY && n==0U);
    }
    else if(id==14U)
    {
        Inject(0U,USART_ISR_FE);
        CHECK(BSP_BT_UART_Recover()==SYS_ERR_TIMEOUT);
        CHECK(BSP_BT_UART_GetDiagnostics(&d)==SYS_OK && d.ready==0U && d.fe==1U && d.last_result==SYS_ERR_TIMEOUT);
        CHECK(BSP_BT_UART_Write(buffer,1U,&n)==SYS_ERR_INVALID_STATE);
    }
    else if(id==16U)
    {
        for(i=0U;i<8192U;i++)
        {
            buffer[0]=(uint8_t)i;
            CHECK(BSP_BT_UART_Write(buffer,1U,&n)==SYS_OK && n==1U);
            Inject((uint8_t)i,0U);
            CHECK(BSP_BT_UART_Read(buffer,1U,&n)==SYS_OK && n==1U && buffer[0]==(uint8_t)i);
        }
        CHECK(BSP_BT_UART_GetDiagnostics(&d)==SYS_OK && d.rx_bytes==8192U && d.tx_bytes==8192U && d.fault==0U);
    }
    else if(id==17U)
    {
        //大请求采用部分进展语义，不读取输入数组之外的数据
        CHECK(BSP_BT_UART_Write(buffer,65U,&n)==SYS_ERR_BUSY && n==64U);
        CHECK(BSP_BT_UART_DeInit()==SYS_OK);
        CHECK(BSP_BT_UART_GetDiagnostics(&d)==SYS_OK && d.tx_discarded==64U && d.ready==0U);
    }
    else return 999U;
    return 0U;
}
