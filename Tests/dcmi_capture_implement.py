from pathlib import Path
import hashlib,json,shutil
r=Path('.');t=r/'Tests/DcmiCapture';t.mkdir(exist_ok=True)
paths=[p for d in ['CM7','CM4','Common','Board','Drivers/STM32H7xx_HAL_Driver'] for p in (r/d).rglob('*') if p.is_file()]+list((r/'MDK-ARM').glob('*.sct'))+[r/'MDK-ARM/h7_mac.uvprojx']
(t/'baseline.json').write_text(json.dumps({str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},indent=2),encoding='utf-8')
for f in ['CM7/BSP/bsp_dcmi.c','CM7/BSP/bsp_dcmi.h']:
 dst=t/'before'/f;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(f,dst)
def read(f):return Path(f).read_bytes().decode('gbk').replace('\r\r\n','\n').replace('\r\n','\n')
def write(f,s):Path(f).write_bytes(s.replace('\n','\r\n').encode('gbk'))
write('CM7/BSP/bsp_dcmi.h','''#ifndef CM7_BSP_DCMI_H
#define CM7_BSP_DCMI_H

#include "sys_result.h"
#include <stdbool.h>

#define BSP_DCMI_EVENT_FRAME (1UL << 0)
#define BSP_DCMI_EVENT_ERROR (1UL << 1)

//只传递结果，不管理帧缓冲所有权
typedef struct
{
	uint32_t flags;
	SysResult_t result;
	uint32_t dcmi_error;
	uint32_t dma_error;
} BspDcmiEvents_t;

//唤醒CM4前预配置引脚，避免GPIOB初始化冲突
void BSP_DCMI_EarlyInit(void);

//仅初始化DCMI和DMA，不启动采集
SysResult_t BSP_DCMI_Init(void);

//主循环调用，buffer须4字节对齐且DMA可访问，数量单位为32位字
//只支持1至65535字，调用方保证缓冲容量和MPU配置有效
SysResult_t BSP_DCMI_StartSnapshot(
	uint32_t *buffer,
	uint32_t word_count
);

//主循环且全局中断开启时调用，停止并恢复HAL状态，保留已有事件
SysResult_t BSP_DCMI_Stop(void);

//原子获取并清除事件，false表示无事件或空指针
//读取错误事件不代表硬件已恢复，错误后需成功Stop再启动
bool BSP_DCMI_TakeEvents(BspDcmiEvents_t *events);

#endif
''')
f='CM7/BSP/bsp_dcmi.c';s=read(f)
s=s.replace('#include "framework_init.h"','#include "framework_init.h"\n#include "platform_time.h"')
s=s.replace('#define DCMI_DMA_IRQ_PRIORITY 5U','#define DCMI_DMA_IRQ_PRIORITY 5U\n#define DCMI_STOP_TIMEOUT_MS 10U\n#define DCMI_ALL_INTERRUPTS (DCMI_IT_FRAME | DCMI_IT_OVR | DCMI_IT_ERR | DCMI_IT_VSYNC | DCMI_IT_LINE)')
pos=s.index('\nstatic void Dcmi_ConfigPin')
s=s[:pos]+'''
//ISR只锁存事件，主循环负责停止和恢复
static volatile uint32_t s_event_flags;
static volatile SysResult_t s_event_result;
static volatile uint32_t s_event_dcmi_error;
static volatile uint32_t s_event_dma_error;
static volatile bool s_capture_active;
static volatile bool s_accept_events;
static volatile bool s_stop_required;
static bool s_control_busy;
'''+s[pos:]
pos=s.index('void DMA1_Stream0_IRQHandler(void)')
s=s[:pos]+'''//仅在临界区、IRQ或已屏蔽两个IRQ时调用
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
	__HAL_DCMI_CLEAR_FLAG(&s_dcmi, DCMI_FLAG_FRAMERI | DCMI_FLAG_OVRRI |
		DCMI_FLAG_ERRRI | DCMI_FLAG_VSYNCRI | DCMI_FLAG_LINERI);
	__HAL_DMA_CLEAR_FLAG(&s_dcmi_dma, DMA_FLAG_FEIF0_4 | DMA_FLAG_DMEIF0_4 |
		DMA_FLAG_TEIF0_4 | DMA_FLAG_HTIF0_4 | DMA_FLAG_TCIF0_4);
	HAL_NVIC_ClearPendingIRQ(DMA1_Stream0_IRQn);
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

	if ( buffer == NULL || ((uint32_t)buffer & 3U) != 0U ||
		word_count == 0U || word_count > 0xFFFFU)
		return SYS_ERR_INVALID_ARG;
	if ( (uint32_t)buffer > 0xFFFFFFFFUL - (word_count * 4U - 1U))
		return SYS_ERR_INVALID_ARG;
	if ( !s_dcmi_ready || __get_IPSR() != 0U || __get_PRIMASK() != 0U)
		return SYS_ERR_INVALID_STATE;

	primask = __get_PRIMASK();
	__disable_irq();
	if ( s_accept_events && !s_stop_required)
		Dcmi_CheckErrors();
	if ( s_stop_required)
	{
		__set_PRIMASK(primask);
		return SYS_ERR_INVALID_STATE;
	}
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

	s_control_busy = true;
	s_accept_events = false;
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

	status = HAL_DMA_DeInit(&s_dcmi_dma);
	if ( status != HAL_OK)
		return Dcmi_HalResult(status);
	status = HAL_DMA_Init(&s_dcmi_dma);
	if ( status != HAL_OK)
		return Dcmi_HalResult(status);
	__HAL_LINKDMA(&s_dcmi, DMA_Handle, s_dcmi_dma);
	status = HAL_DCMI_DeInit(&s_dcmi);
	if ( status != HAL_OK)
		return Dcmi_HalResult(status);
	status = HAL_DCMI_Init(&s_dcmi);
	if ( status != HAL_OK)
		return Dcmi_HalResult(status);

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
'''
write(f,s)
print('Implemented Snapshot/Stop/TakeEvents and callbacks; only BSP .c/.h modified.')
