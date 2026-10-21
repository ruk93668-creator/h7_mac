#include "bsp_dcmi.h"
#include "framework_init.h"
#include "platform_time.h"
#include "stm32h7xx_hal.h"
#include <stdbool.h>

//沿用V0测试工程优先级，CM7 SysTick优先级为15
#define DCMI_IRQ_PRIORITY 5U
#define DCMI_DMA_IRQ_PRIORITY 5U
#define DCMI_STOP_TIMEOUT_MS 10U
#define DCMI_ALL_INTERRUPTS (DCMI_IT_FRAME | DCMI_IT_OVR | DCMI_IT_ERR | DCMI_IT_VSYNC | DCMI_IT_LINE)

static DCMI_HandleTypeDef s_dcmi;
static DMA_HandleTypeDef s_dcmi_dma;
static bool s_dcmi_pins_ready;
static bool s_dcmi_ready;

//ISR只锁存事件，主循环负责停止和恢复
//待主循环读取的完成或错误事件
static volatile uint32_t s_event_flags;
//待交付事件的结果，保留首次错误
static volatile SysResult_t s_event_result;
//累积DCMI具体错误位
static volatile uint32_t s_event_dcmi_error;
//累积DMA具体错误位
static volatile uint32_t s_event_dma_error;
//当前是否正在等待整帧采集完成
static volatile bool s_capture_active;
//是否接收采集回调，Stop后忽略迟到回调
static volatile bool s_accept_events;
//出错后要求先Stop恢复，再允许启动
static volatile bool s_stop_required;
//Start或Stop正在执行，防止控制流程重入
static bool s_control_busy;

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

//仅在临界区、IRQ或已屏蔽两个IRQ时调用
static void Dcmi_RecordError(SysResult_t result)
{
	s_event_dcmi_error |= s_dcmi.ErrorCode;
	s_event_dma_error |= s_dcmi_dma.ErrorCode;
	s_event_flags &= ~BSP_DCMI_EVENT_FRAME;
	s_event_flags |= BSP_DCMI_EVENT_ERROR;
	if ( s_event_result == SYS_OK)
		s_event_result = result;
	s_stop_required = true;
}

//捕获尚未进入HAL回调的错误，防止误交付完成事件
static bool Dcmi_CheckErrors(void)
{
	uint32_t dcmi_flags = DCMI->RISR;
	uint32_t dma_flags = DMA1->LISR;
	uint32_t dcmi_error = s_dcmi.ErrorCode;
	uint32_t dma_error = s_dcmi_dma.ErrorCode;

	if ( (dcmi_flags & DCMI_FLAG_ERRRI) != 0U)
		dcmi_error |= HAL_DCMI_ERROR_SYNC;
	if ( (dcmi_flags & DCMI_FLAG_OVRRI) != 0U)
		dcmi_error |= HAL_DCMI_ERROR_OVR;
	
	if ( (dma_flags & DMA_FLAG_TEIF0_4) != 0U &&
		(DMA1_Stream0->CR & DMA_IT_TE) != 0U)
		dma_error |= HAL_DMA_ERROR_TE;
	if ( (dma_flags & DMA_FLAG_DMEIF0_4) != 0U &&
		(DMA1_Stream0->CR & DMA_IT_DME) != 0U)
		dma_error |= HAL_DMA_ERROR_DME;

	if ( dcmi_error == HAL_DCMI_ERROR_NONE && dma_error == HAL_DMA_ERROR_NONE)
		return false;

	s_event_dcmi_error |= dcmi_error;
	s_event_dma_error |= dma_error;
	Dcmi_RecordError(SYS_ERR_IO);
	return true;
}

static void Dcmi_ClearPending(void)
{
	//清除DCMI帧、溢出、同步错误、场同步和行标志
	__HAL_DCMI_CLEAR_FLAG(&s_dcmi, DCMI_FLAG_FRAMERI | DCMI_FLAG_OVRRI |
		DCMI_FLAG_ERRRI | DCMI_FLAG_VSYNCRI | DCMI_FLAG_LINERI);
	
	//清除DMA FIFO、直接模式、传输错误及半传输、传输完成标志
	__HAL_DMA_CLEAR_FLAG(&s_dcmi_dma, DMA_FLAG_FEIF0_4 | DMA_FLAG_DMEIF0_4 |
		DMA_FLAG_TEIF0_4 | DMA_FLAG_HTIF0_4 | DMA_FLAG_TCIF0_4);
	
	//清除NVIC中挂起的DMA中断请求
	HAL_NVIC_ClearPendingIRQ(DMA1_Stream0_IRQn);
	
	//清除NVIC中挂起的DCMI中断请求
	HAL_NVIC_ClearPendingIRQ(DCMI_IRQn);
}

SysResult_t BSP_DCMI_StartSnapshot(
	uint32_t *buffer,
	uint32_t word_count
)
{
	uint32_t primask;
	HAL_StatusTypeDef status;
	SysResult_t result;

	//检查缓冲区、4字节对齐和DMA传输字数
	if ( buffer == NULL || ((uint32_t)buffer & 3U) != 0U ||
		word_count == 0U || word_count > 0xFFFFU)
		return SYS_ERR_INVALID_ARG;
	//防止缓冲区末地址超出32位地址范围
	if ( (uint32_t)buffer > 0xFFFFFFFFUL - (word_count * 4U - 1U))
		return SYS_ERR_INVALID_ARG;
	//必须已初始化，在主循环且全局中断开启时调用
	if ( !s_dcmi_ready || __get_IPSR() != 0U || __get_PRIMASK() != 0U)
		return SYS_ERR_INVALID_STATE;

	//保存中断状态，保护检查和启动过程
	primask = __get_PRIMASK();
	__disable_irq();
	
	//补查上一轮尚未进入HAL回调的硬件错误
	if ( s_accept_events && !s_stop_required)
		Dcmi_CheckErrors();
	
	//存在未恢复错误，恢复中断后返回
	if ( s_stop_required)
	{
		__set_PRIMASK(primask);
		return SYS_ERR_INVALID_STATE;
	}
	//控制流程、采集硬件和HAL句柄都空闲才允许启动
	if ( s_control_busy || s_capture_active ||
		(DMA1_Stream0->CR & DMA_SxCR_EN) != 0U ||
		(DCMI->CR & DCMI_CR_CAPTURE) != 0U ||
		s_dcmi.State != HAL_DCMI_STATE_READY ||
		s_dcmi_dma.State != HAL_DMA_STATE_READY ||
		s_dcmi.Lock != HAL_UNLOCKED || s_dcmi_dma.Lock != HAL_UNLOCKED)
	{
		__set_PRIMASK(primask);
		return SYS_ERR_BUSY;
	}

	//占用控制流程，暂不接收采集回调
	s_control_busy = true;
	s_accept_events = false;
	//关闭DCMI及其中断，清除DCMI、DMA和NVIC挂起标志
	__HAL_DCMI_DISABLE_IT(&s_dcmi, DCMI_ALL_INTERRUPTS);
	__HAL_DCMI_DISABLE(&s_dcmi);
	Dcmi_ClearPending();

	//新一轮启动才清除上一轮事件和HAL错误
	s_event_flags = 0U;
	s_event_result = SYS_OK;
	s_event_dcmi_error = 0U;
	s_event_dma_error = 0U;
	s_dcmi.ErrorCode = HAL_DCMI_ERROR_NONE;
	s_dcmi_dma.ErrorCode = HAL_DMA_ERROR_NONE;
	s_capture_active = true;
	s_accept_events = true;

	//Snapshot结束会关闭这些中断，每轮都恢复错误检测
	__HAL_DCMI_ENABLE_IT(&s_dcmi, DCMI_IT_ERR | DCMI_IT_OVR);
	status = HAL_DCMI_Start_DMA(&s_dcmi, DCMI_MODE_SNAPSHOT,
		(uint32_t)buffer, word_count);
	result = Dcmi_HalResult(status);
	if ( result != SYS_OK)
	{
		//启动失败也可能已使能DCMI，停止数据源并要求显式Stop
		DCMI->CR &= ~DCMI_CR_CAPTURE;
		__HAL_DCMI_DISABLE(&s_dcmi);
		__HAL_DMA_DISABLE(&s_dcmi_dma);
		Dcmi_RecordError(result);
	}
	s_control_busy = false;
	__set_PRIMASK(primask);
	return result;
}

static SysResult_t Dcmi_WaitStopped(void)
{
	uint32_t start = Platform_TimeGetMs();

	while ((DMA1_Stream0->CR & DMA_SxCR_EN) != 0U ||
		(DCMI->CR & (DCMI_CR_CAPTURE | DCMI_CR_ENABLE)) != 0U)
	{
		if ( (uint32_t)(Platform_TimeGetMs() - start) >= DCMI_STOP_TIMEOUT_MS)
			return SYS_ERR_TIMEOUT;
	}
	return SYS_OK;
}

//只在确认硬件停止后重建HAL句柄，处理遗留ABORT/ERROR和锁状态
static SysResult_t Dcmi_RecoverHal(void)
{
	HAL_StatusTypeDef status;

	//复位DMA配置，清除遗留状态
	status = HAL_DMA_DeInit(&s_dcmi_dma);
	if ( status != HAL_OK)
		return Dcmi_HalResult(status);
	//重新配置DMA，恢复就绪状态
	status = HAL_DMA_Init(&s_dcmi_dma);
	if ( status != HAL_OK)
		return Dcmi_HalResult(status);
	//重新关联DCMI与DMA句柄
	__HAL_LINKDMA(&s_dcmi, DMA_Handle, s_dcmi_dma);
	//复位DCMI配置，清除遗留状态
	status = HAL_DCMI_DeInit(&s_dcmi);
	if ( status != HAL_OK)
		return Dcmi_HalResult(status);
	//重新配置DCMI，恢复就绪状态
	status = HAL_DCMI_Init(&s_dcmi);
	if ( status != HAL_OK)
		return Dcmi_HalResult(status);

	//确认两个句柄均已就绪且未被锁定
	if ( s_dcmi.State != HAL_DCMI_STATE_READY ||
		s_dcmi_dma.State != HAL_DMA_STATE_READY ||
		s_dcmi.Lock != HAL_UNLOCKED || s_dcmi_dma.Lock != HAL_UNLOCKED)
		return SYS_ERR_INVALID_STATE;
	return SYS_OK;
}

SysResult_t BSP_DCMI_Stop(void)
{
	uint32_t primask;
	HAL_StatusTypeDef status;
	SysResult_t result;

	if ( !s_dcmi_ready || __get_IPSR() != 0U || __get_PRIMASK() != 0U)
		return SYS_ERR_INVALID_STATE;

	primask = __get_PRIMASK();
	__disable_irq();
	if ( s_control_busy)
	{
		__set_PRIMASK(primask);
		return SYS_ERR_BUSY;
	}
	s_control_busy = true;
	if ( s_accept_events && !s_stop_required)
		Dcmi_CheckErrors();
	s_accept_events = false;
	s_capture_active = false;
	HAL_NVIC_DisableIRQ(DMA1_Stream0_IRQn);
	HAL_NVIC_DisableIRQ(DCMI_IRQn);
	__HAL_DCMI_DISABLE_IT(&s_dcmi, DCMI_ALL_INTERRUPTS);
	__set_PRIMASK(primask);

	//ABORT状态下HAL_Stop可能跳过DMA停止，先主动请求关闭Stream
	__HAL_DMA_DISABLE(&s_dcmi_dma);
	status = HAL_DCMI_Stop(&s_dcmi);
	DCMI->CR &= ~DCMI_CR_CAPTURE;
	__HAL_DCMI_DISABLE(&s_dcmi);
	__HAL_DMA_DISABLE(&s_dcmi_dma);

	//保持SysTick运行，等待硬件真正停止，不依赖HAL_Stop返回值
	result = Dcmi_WaitStopped();
	if ( result == SYS_OK)
		result = Dcmi_RecoverHal();

	primask = __get_PRIMASK();
	__disable_irq();
	if ( result == SYS_OK)
	{
		__HAL_DCMI_DISABLE_IT(&s_dcmi, DCMI_ALL_INTERRUPTS);
		Dcmi_ClearPending();
		if ( status != HAL_OK)
		{
			result = Dcmi_HalResult(status);
			Dcmi_RecordError(result);
		}
		//硬件和HAL状态已恢复，保留已锁存事件供上层读取
		s_stop_required = false;
		HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
		HAL_NVIC_EnableIRQ(DCMI_IRQn);
	}
	else
	{
		//未恢复时保持IRQ关闭，禁止直接开始下一轮
		Dcmi_RecordError(result);
	}
	s_control_busy = false;
	__set_PRIMASK(primask);
	return result;
}

bool BSP_DCMI_TakeEvents(BspDcmiEvents_t *events)
{
	uint32_t primask;

	if ( events == NULL)
		return false;

	primask = __get_PRIMASK();
	__disable_irq();
	//完成交付前再检查尚未处理的硬件错误，错误优先
	if ( s_accept_events && (s_event_flags & BSP_DCMI_EVENT_FRAME) != 0U)
		Dcmi_CheckErrors();
	if ( s_event_flags == 0U)
	{
		__set_PRIMASK(primask);
		return false;
	}

	events->flags = s_event_flags;
	events->result = s_event_result;
	events->dcmi_error = s_event_dcmi_error;
	events->dma_error = s_event_dma_error;
	s_event_flags = 0U;
	s_event_result = SYS_OK;
	s_event_dcmi_error = 0U;
	s_event_dma_error = 0U;
	__set_PRIMASK(primask);
	return true;
}

void HAL_DCMI_FrameEventCallback(DCMI_HandleTypeDef *hdcmi)
{
	if ( hdcmi != &s_dcmi || !s_accept_events || !s_capture_active || s_stop_required)
		return;
	if ( Dcmi_CheckErrors())
		return;

	//只交付DMA数量完成后的帧事件，READY本身不是整帧完成证明
	if ( s_dcmi_dma.State != HAL_DMA_STATE_READY ||
		DMA1_Stream0->NDTR != 0U || (DMA1_Stream0->CR & DMA_SxCR_EN) != 0U)
	{
		Dcmi_RecordError(SYS_ERR_PROTOCOL);
		return;
	}
	s_event_flags |= BSP_DCMI_EVENT_FRAME;
	s_capture_active = false;
}

void HAL_DCMI_ErrorCallback(DCMI_HandleTypeDef *hdcmi)
{
	if ( hdcmi != &s_dcmi || !s_accept_events)
		return;
	Dcmi_RecordError(SYS_ERR_IO);
}

void DMA1_Stream0_IRQHandler(void)
{
	if ( s_accept_events)
		Dcmi_CheckErrors();
	HAL_DMA_IRQHandler(&s_dcmi_dma);
}

void DCMI_IRQHandler(void)
{
	//HAL异步Abort不保证立即回调，先记录当前错误再进入HAL
	if ( s_accept_events)
		Dcmi_CheckErrors();
	HAL_DCMI_IRQHandler(&s_dcmi);
}

//系统时钟完成后初始化底层，不自动开始采集
FRAMEWORK_INIT_REGISTER(
	BSP_DCMI_Init,
	INIT_LEVEL_BUS
);
