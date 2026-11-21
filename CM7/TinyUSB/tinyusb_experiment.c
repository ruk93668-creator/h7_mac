/* Isolated RAM-only firmware port. No legacy USB, camera, SD or second USB task. */
#include "tinyusb_experiment.h"
#include "tinyusb_ram.h"
#include "stm32h7xx_hal.h"
#include "tusb.h"
#include "device/usbd_pvt.h"
#include "task.h"
#if !defined(TINYUSB_RAM_EXPERIMENT) || !configUSE_MUTEXES || !configSUPPORT_STATIC_ALLOCATION
#error Experimental port requires static FreeRTOS mutexes and isolated build
#endif
volatile TinyUSBExperimentDebug_t g_tinyusb_experiment;

bool TinyUSB_ExperimentPrepare(void)
{
    GPIO_InitTypeDef gpio = {0};
    RCC_OscInitTypeDef osc = {0};
    RCC_PeriphCLKInitTypeDef clock = {0};
    RCC_CRSInitTypeDef crs = {0};
    uint32_t first;
    SystemCoreClockUpdate();
    g_tinyusb_experiment.cpu_hz = SystemCoreClock;
    if (SystemCoreClock != 480000000U) return false;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB(); __ISB();
    first = DWT->CYCCNT;
    __NOP(); __NOP(); __NOP(); __NOP();
    g_tinyusb_experiment.dwt_first = first;
    g_tinyusb_experiment.dwt_second = DWT->CYCCNT;
    if (first == g_tinyusb_experiment.dwt_second) return false;
    /* Same HSI48/CRS selection as existing BSP; never change PLL or HCLK. */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI48;
    osc.HSI48State = RCC_HSI48_ON; osc.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) return false;
    clock.PeriphClockSelection = RCC_PERIPHCLK_USB;
    clock.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
    if (HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK) return false;
    __HAL_RCC_CRS_CLK_ENABLE();
    crs.Prescaler = RCC_CRS_SYNC_DIV1; crs.Source = RCC_CRS_SYNC_SOURCE_USB2;
    crs.Polarity = RCC_CRS_SYNC_POLARITY_RISING;
    crs.ReloadValue = __HAL_RCC_CRS_RELOADVALUE_CALCULATE(48000000U,1000U);
    crs.ErrorLimitValue = RCC_CRS_ERRORLIMIT_DEFAULT;
    crs.HSI48CalibrationValue = RCC_CRS_HSI48CALIBRATION_DEFAULT;
    HAL_RCCEx_CRSConfig(&crs);
    HAL_PWREx_DisableUSBReg(); HAL_PWREx_EnableUSBVoltageDetector();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_11 | GPIO_PIN_12; gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL; gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF10_OTG2_FS; HAL_GPIO_Init(GPIOA,&gpio);
    __HAL_RCC_USB2_OTG_FS_CLK_ENABLE();
    __HAL_RCC_USB2_OTG_FS_CLK_SLEEP_ENABLE();
    __HAL_RCC_USB2_OTG_FS_ULPI_CLK_SLEEP_DISABLE();
    HAL_NVIC_DisableIRQ(OTG_FS_IRQn); HAL_NVIC_ClearPendingIRQ(OTG_FS_IRQn);
    HAL_NVIC_SetPriority(OTG_FS_IRQn,configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY,0);
    g_tinyusb_experiment.irq_priority = NVIC_GetPriority(OTG_FS_IRQn);
    return g_tinyusb_experiment.irq_priority == configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY;
}

/* One strong vector owner; original BSP/PCD objects are excluded from this project. */
void OTG_FS_IRQHandler(void) { tud_int_handler(0); }

void TinyUSB_ExperimentRun(void)
{
    tusb_rhport_init_t init = { .role=TUSB_ROLE_DEVICE, .speed=TUSB_SPEED_FULL };
    g_tinyusb_experiment.basepri = __get_BASEPRI();
    configASSERT(NVIC_GetPriority(OTG_FS_IRQn) >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
    g_tinyusb_experiment.initialized = tud_rhport_init(0,&init);
    for (;;)
    {
        if (g_tinyusb_experiment.initialized && !g_tinyusb_experiment.stopped)
        {
            (void)tud_task_ext(1,false);
            TinyUSB_RamPoll();
        }
        /* Optional debugger commands only. No hardware test is run automatically. */
        if (g_tinyusb_experiment.command == 1U)
        {
            bool drained = true;
            if (usbd_edpt_busy(0,0x02)) drained = usbd_edpt_abort(0,0x02);
            if (usbd_edpt_busy(0,0x82)) drained = usbd_edpt_abort(0,0x82) && drained;
            if (drained && TinyUSB_RamDrained() && tud_deinit(0))
            { g_tinyusb_experiment.stopped=1; g_tinyusb_experiment.command=0; }
        }
        else if (g_tinyusb_experiment.command == 2U && g_tinyusb_experiment.stopped)
        {
            if (tud_rhport_init(0,&init))
            { g_tinyusb_experiment.stopped=0; g_tinyusb_experiment.command=0; }
        }
        if (usbd_device_failed()) g_tinyusb_experiment.failures++;
        g_tinyusb_experiment.polls++;
        vTaskDelay(1); /* bounded revisit even when no DCD event is queued */
    }
}
