#include "bsp_dcmi.h"
#include "framework_init.h"
#include "stm32h7xx_hal.h"
#include <stdbool.h>

//沿用V0测试工程优先级，CM7 SysTick优先级为15
#define DCMI_IRQ_PRIORITY 5U
#define DCMI_DMA_IRQ_PRIORITY 5U

static DCMI_HandleTypeDef s_dcmi;
static DMA_HandleTypeDef s_dcmi_dma;
static bool s_dcmi_pins_ready;
static bool s_dcmi_ready;

static void Dcmi_ConfigPin(
	GPIO_TypeDef *port,
	uint32_t pins
)
{
	GPIO_InitTypeDef gpio = {0};

	gpio.Pin = pins;
	gpio.Mode = GPIO_MODE_AF_PP;
	gpio.Pull = GPIO_NOPULL;
	gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
	gpio.Alternate = GPIO_AF13_DCMI;
	HAL_GPIO_Init(port, &gpio);
}

void BSP_DCMI_EarlyInit(void)
{
	if ( s_dcmi_pins_ready)
		return;

	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();
	__HAL_RCC_GPIOC_CLK_ENABLE();
	__HAL_RCC_GPIOE_CLK_ENABLE();
	__HAL_RCC_GPIOG_CLK_ENABLE();
	__HAL_RCC_GPIOH_CLK_ENABLE();

	//V0 D0、D1
	Dcmi_ConfigPin(GPIOC, GPIO_PIN_6 | GPIO_PIN_7);
	//V0 D2、D3
	Dcmi_ConfigPin(GPIOG, GPIO_PIN_10);
	Dcmi_ConfigPin(GPIOH, GPIO_PIN_12);
	//V0 D4、D6、D7
	Dcmi_ConfigPin(GPIOE, GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6);
	//V0 D5、VSYNC，提前配置以避开CM4的PB10/PB11初始化
	Dcmi_ConfigPin(GPIOB, GPIO_PIN_6 | GPIO_PIN_7);
	//V0 HSYNC、PCLK
	Dcmi_ConfigPin(GPIOA, GPIO_PIN_4 | GPIO_PIN_6);

	s_dcmi_pins_ready = true;
}

//HAL初始化时保证GPIO和外设时钟就绪
void HAL_DCMI_MspInit(DCMI_HandleTypeDef *hdcmi)
{
	if ( hdcmi->Instance != DCMI)
		return;

	BSP_DCMI_EarlyInit();
	__HAL_RCC_DCMI_CLK_ENABLE();
}

static SysResult_t Dcmi_HalResult(HAL_StatusTypeDef status)
{
	if ( status == HAL_OK)
		return SYS_OK;
	if ( status == HAL_BUSY)
		return SYS_ERR_BUSY;
	if ( status == HAL_TIMEOUT)
		return SYS_ERR_TIMEOUT;
	return SYS_ERR_IO;
}

SysResult_t BSP_DCMI_Init(void)
{
	HAL_StatusTypeDef status;

	if ( s_dcmi_ready)
		return SYS_OK;

	BSP_DCMI_EarlyInit();
	__HAL_RCC_DMA1_CLK_ENABLE();
	__HAL_RCC_DCMI_CLK_ENABLE();

	//初始化不接管正在运行的采集或DMA
	if ( (DMA1_Stream0->CR & DMA_SxCR_EN) != 0U ||
		(DCMI->CR & (DCMI_CR_ENABLE | DCMI_CR_CAPTURE)) != 0U)
		return SYS_ERR_BUSY;

	HAL_NVIC_DisableIRQ(DMA1_Stream0_IRQn);
	HAL_NVIC_DisableIRQ(DCMI_IRQn);

	//外设到内存，32位传输，普通模式，不启动Stream
	//选择DMA1的Stream0
	s_dcmi_dma.Instance = DMA1_Stream0;
	//选择DCMI请求源，由DMAMUX路由到该Stream
	s_dcmi_dma.Init.Request = DMA_REQUEST_DCMI;
	//数据从DCMI外设搬运到内存
	s_dcmi_dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
	//外设地址固定，始终读取DCMI数据寄存器
	s_dcmi_dma.Init.PeriphInc = DMA_PINC_DISABLE;
	//内存地址递增，连续保存接收数据
	s_dcmi_dma.Init.MemInc = DMA_MINC_ENABLE;
	//每次从外设读取32位数据
	s_dcmi_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
	//每次向内存写入32位数据
	s_dcmi_dma.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
	//普通模式，完成指定长度后停止，不循环传输
	s_dcmi_dma.Init.Mode = DMA_NORMAL;
	//设置DMA硬件仲裁优先级为高，与NVIC优先级独立
	s_dcmi_dma.Init.Priority = DMA_PRIORITY_HIGH;
	//关闭DMA FIFO，使用直接模式
	s_dcmi_dma.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
	//内存侧使用单次传输，不使用突发传输
	s_dcmi_dma.Init.MemBurst = DMA_MBURST_SINGLE;
	//外设侧使用单次传输，不使用突发传输
	s_dcmi_dma.Init.PeriphBurst = DMA_PBURST_SINGLE;
	status = HAL_DMA_Init(&s_dcmi_dma);
	if ( status != HAL_OK)
		return Dcmi_HalResult(status);

	//8位DVP、硬件同步，PCLK上升沿采样
	s_dcmi.Instance = DCMI;
	s_dcmi.Init.SynchroMode = DCMI_SYNCHRO_HARDWARE;
	s_dcmi.Init.PCKPolarity = DCMI_PCKPOLARITY_RISING;
	s_dcmi.Init.VSPolarity = DCMI_VSPOLARITY_HIGH;
	s_dcmi.Init.HSPolarity = DCMI_HSPOLARITY_HIGH;
	s_dcmi.Init.CaptureRate = DCMI_CR_ALL_FRAME;
	s_dcmi.Init.ExtendedDataMode = DCMI_EXTEND_DATA_8B;
	s_dcmi.Init.JPEGMode = DCMI_JPEG_DISABLE;
	s_dcmi.Init.ByteSelectMode = DCMI_BSM_ALL;
	s_dcmi.Init.ByteSelectStart = DCMI_OEBS_ODD;
	s_dcmi.Init.LineSelectMode = DCMI_LSM_ALL;
	s_dcmi.Init.LineSelectStart = DCMI_OELS_ODD;

	//同时建立DCMI到DMA、DMA到Parent的关联
	__HAL_LINKDMA(&s_dcmi, DMA_Handle, s_dcmi_dma);
	status = HAL_DCMI_Init(&s_dcmi);
	if ( status != HAL_OK)
	{
		//失败不开放NVIC，释放本次DMA初始化
		if ( HAL_DMA_DeInit(&s_dcmi_dma) != HAL_OK)
			return SYS_ERR_IO;
		return Dcmi_HalResult(status);
	}

	//清除旧标志和挂起状态，再开放NVIC
	__HAL_DCMI_CLEAR_FLAG(&s_dcmi, DCMI_FLAG_FRAMERI | DCMI_FLAG_OVRRI |
		DCMI_FLAG_ERRRI | DCMI_FLAG_VSYNCRI | DCMI_FLAG_LINERI);
	HAL_NVIC_ClearPendingIRQ(DMA1_Stream0_IRQn);
	HAL_NVIC_ClearPendingIRQ(DCMI_IRQn);
	HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, DCMI_DMA_IRQ_PRIORITY, 0U);
	HAL_NVIC_SetPriority(DCMI_IRQn, DCMI_IRQ_PRIORITY, 0U);
	HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
	HAL_NVIC_EnableIRQ(DCMI_IRQn);

	s_dcmi_ready = true;
	return SYS_OK;
}

void DMA1_Stream0_IRQHandler(void)
{
	HAL_DMA_IRQHandler(&s_dcmi_dma);
}

void DCMI_IRQHandler(void)
{
	HAL_DCMI_IRQHandler(&s_dcmi);
}

//系统时钟完成后初始化底层，不依赖帧缓冲
FRAMEWORK_INIT_REGISTER(
	BSP_DCMI_Init,
	INIT_LEVEL_BUS
);
