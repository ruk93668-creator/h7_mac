/* Execute actual BSP and Initcall code with test-only HAL functions. */
#include "bsp_iic.h"
#include "framework_init.h"
#include "stm32h7xx_hal.h"
#include <stddef.h>
void __aeabi_memclr4(void *dest,unsigned size)
{
    volatile unsigned char *p=(volatile unsigned char *)dest;
    while (size) { *p++=0U;size--; }
}
void __aeabi_memclr8(void *dest,unsigned size) { __aeabi_memclr4(dest,size); }
#include "../../CM4/BSP/bsp_iic.c"
#include "../../CM4/Framework/framework_init.c"

typedef struct {
    uint32_t failure_line;
    int32_t result;
    int32_t nested_result;
    uint32_t rcc_calls,init_calls,analog_calls,digital_calls,deinit_calls;
    uint32_t gpio_calls,gpio_deinit_calls,device_stage_calls,trace;
} TestResult;
volatile TestResult g_bsp_result;
static uint32_t frequency=120000000U;
static HAL_StatusTypeDef rcc_status=HAL_OK,init_status=HAL_OK,analog_status=HAL_OK,digital_status=HAL_OK;
static unsigned gpio_clock,i2c_clock,pins_ready,invalid,reenter;
static void Step(unsigned id) { g_bsp_result.trace=g_bsp_result.trace*10U+id; }

HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *clock)
{
    g_bsp_result.rcc_calls++;Step(1U);
    if (clock->PeriphClockSelection!=RCC_PERIPHCLK_I2C2 || clock->I2c123ClockSelection!=RCC_I2C123CLKSOURCE_D2PCLK1) invalid=1U;
    return rcc_status;
}
uint32_t HAL_RCCEx_GetPeriphCLKFreq(uint64_t peripheral)
{
    if (peripheral!=RCC_PERIPHCLK_I2C2) invalid=1U;
    return frequency;
}
void Test_GPIOClock(void) { gpio_clock=1U;Step(2U); }
void Test_I2CClock(unsigned enabled) { i2c_clock=enabled;if(enabled) Step(4U); }
void HAL_GPIO_Init(void *port,GPIO_InitTypeDef *gpio)
{
    g_bsp_result.gpio_calls++;Step(3U);
    if (!gpio_clock || port!=GPIOB || gpio->Pin!=(GPIO_PIN_10|GPIO_PIN_11) || gpio->Mode!=GPIO_MODE_AF_OD || gpio->Pull!=GPIO_NOPULL || gpio->Speed!=GPIO_SPEED_FREQ_LOW || gpio->Alternate!=GPIO_AF4_I2C2) invalid=1U;
    pins_ready=1U;
    if (reenter) { reenter=0U;g_bsp_result.nested_result=BSP_IIC_Init(); }
}
void HAL_GPIO_DeInit(void *port,uint32_t pins)
{
    if(port!=GPIOB || pins!=(GPIO_PIN_10|GPIO_PIN_11)) invalid=1U;
    g_bsp_result.gpio_deinit_calls++;pins_ready=0U;
}
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *handle)
{
    g_bsp_result.init_calls++;
    if (handle->State==0U) HAL_I2C_MspInit(handle);
    Step(5U);
    if (!gpio_clock || !pins_ready || !i2c_clock || handle!=&hi2c2 || handle->Instance!=I2C2 || handle->Init.Timing!=0x307075B1U || handle->Init.AddressingMode!=I2C_ADDRESSINGMODE_7BIT || handle->Init.OwnAddress1!=0U || handle->Init.DualAddressMode!=I2C_DUALADDRESS_DISABLE || handle->Init.OwnAddress2!=0U || handle->Init.OwnAddress2Masks!=I2C_OA2_NOMASK || handle->Init.GeneralCallMode!=I2C_GENERALCALL_DISABLE || handle->Init.NoStretchMode!=I2C_NOSTRETCH_DISABLE) invalid=1U;
    handle->State=1U;
    return init_status;
}
HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef *handle)
{
    g_bsp_result.deinit_calls++;HAL_I2C_MspDeInit(handle);handle->State=0U;return HAL_OK;
}
HAL_StatusTypeDef HAL_I2CEx_ConfigAnalogFilter(I2C_HandleTypeDef *handle,uint32_t value)
{
    g_bsp_result.analog_calls++;Step(6U);
    if(handle!=&hi2c2 || value!=I2C_ANALOGFILTER_ENABLE) invalid=1U;
    return analog_status;
}
HAL_StatusTypeDef HAL_I2CEx_ConfigDigitalFilter(I2C_HandleTypeDef *handle,uint32_t value)
{
    g_bsp_result.digital_calls++;Step(7U);
    if(handle!=&hi2c2 || value!=0U) invalid=1U;
    return digital_status;
}
static SysResult_t Test_DeviceStage(void)
{
    g_bsp_result.device_stage_calls++;
    return s_initialized && pins_ready && i2c_clock ? SYS_OK : SYS_ERR_INVALID_STATE;
}
FRAMEWORK_INIT_REGISTER(Test_DeviceStage,INIT_LEVEL_DEVICE);
#define CHECK(c) do { if(!(c)) { g_bsp_result.failure_line=__LINE__;return; } } while(0)

static void Run(unsigned id)
{
    if(id==15U) {
        I2C_HandleTypeDef other={0};
        other.Instance=(void *)0x1234U;
        HAL_I2C_MspInit(NULL);HAL_I2C_MspDeInit(NULL);
        HAL_I2C_MspInit(&other);HAL_I2C_MspDeInit(&other);
        CHECK(!gpio_clock && !i2c_clock && !pins_ready);return;
    }
    if(id==19U || id==20U) {
        if(id==20U) rcc_status=HAL_ERROR;
        g_bsp_result.result=Framework_InitRuntime();
        CHECK(g_bsp_result.result==(id==19U ? SYS_OK : SYS_ERR_IO));
        CHECK(g_bsp_result.device_stage_calls==(id==19U ? 1U : 0U));return;
    }
    if(id==18U) {
        CHECK(g_framework_init_BSP_IIC_Init.level==INIT_LEVEL_BUS);
        CHECK(g_framework_init_BSP_IIC_Init.init==BSP_IIC_Init);return;
    }
    if(id==1U) frequency=80000000U;
    if(id>=2U && id<=13U) {
        HAL_StatusTypeDef status=(id-2U)%3U==0U ? HAL_ERROR : ((id-2U)%3U==1U ? HAL_BUSY : HAL_TIMEOUT);
        if(id<=4U) rcc_status=status;
        else if(id<=7U) init_status=status;
        else if(id<=10U) analog_status=status;
        else digital_status=status;
    }
    if(id==14U) reenter=1U;
    g_bsp_result.result=BSP_IIC_Init();
    if(id==1U) { CHECK(g_bsp_result.result==SYS_ERR_INVALID_STATE && !g_bsp_result.init_calls && !gpio_clock);return; }
    if(id>=2U && id<=13U) {
        SysResult_t expected=(id-2U)%3U==0U ? SYS_ERR_IO : ((id-2U)%3U==1U ? SYS_ERR_BUSY : SYS_ERR_TIMEOUT);
        CHECK(g_bsp_result.result==expected && !s_initialized && !s_initializing);
        if(id<=4U) { CHECK(!g_bsp_result.init_calls && !g_bsp_result.deinit_calls); }
        else {
            CHECK(g_bsp_result.deinit_calls==1U && !pins_ready && !i2c_clock && hi2c2.State==0U);
            if(id<=7U) CHECK(!g_bsp_result.analog_calls && !g_bsp_result.digital_calls);
            else if(id<=10U) CHECK(!g_bsp_result.digital_calls);
        }
        rcc_status=init_status=analog_status=digital_status=HAL_OK;
        CHECK(BSP_IIC_Init()==SYS_OK && s_initialized && pins_ready && i2c_clock);return;
    }
    CHECK(g_bsp_result.result==SYS_OK && s_initialized && !s_initializing);
    CHECK(g_bsp_result.trace==1234567U && g_bsp_result.init_calls==1U && g_bsp_result.analog_calls==1U && g_bsp_result.digital_calls==1U);
    if(id==14U) CHECK(g_bsp_result.nested_result==SYS_ERR_BUSY);
    if(id==16U) {
        frequency=80000000U;
        CHECK(BSP_IIC_Init()==SYS_ERR_INVALID_STATE && g_bsp_result.init_calls==1U);return;
    }
    if(id==17U) {
        HAL_I2C_MspDeInit(&hi2c2);CHECK(!pins_ready && !i2c_clock && gpio_clock);return;
    }
    CHECK(BSP_IIC_Init()==SYS_OK && g_bsp_result.rcc_calls==1U && g_bsp_result.init_calls==1U && g_bsp_result.trace==1234567U);
}
uint32_t Bsp_TestRun(unsigned id)
{
    Run(id);
    if(invalid && !g_bsp_result.failure_line) g_bsp_result.failure_line=__LINE__;
    return g_bsp_result.failure_line;
}
