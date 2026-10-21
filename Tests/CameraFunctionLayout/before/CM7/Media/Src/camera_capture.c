#include "camera_capture.h"
#include "bsp_dcmi.h"
#include "platform_time.h"
#include <stddef.h>
#include <stdint.h>

//图像缓冲区数量
#define CAMERA_STREAM_BUFFER_COUNT     3U

//单个缓冲区容量，容纳160x120的RGB565或YUV422图像
#define CAMERA_STREAM_BUFFER_CAPACITY  (160U * 120U * 2U)

//缓冲区可用SDRAM范围的结束地址，不包含该地址
#define CAMERA_STREAM_SDRAM_END         0xD0020000UL

//缓冲区起始地址按32字节对齐
#define CAMERA_STREAM_BUFFER_ALIGN     32U

//无效缓冲区索引，表示未关联缓冲区
#define CAMERA_BUFFER_INDEX_INVALID    UINT8_MAX

//Media管理的SDRAM缓冲区
#define CAMERA_BUFFER_BASE      0xD0000400UL
#define CAMERA_BUFFER_POOL_SIZE (63U * 1024U)

//DMA单次传输最大Word数量
#define CAMERA_DMA_MAX_WORDS        0xFFFFU

//单帧采集超时时间，单位ms
#define CAMERA_CAPTURE_TIMEOUT_MS   100U

//采集状态
typedef enum
{
	CAMERA_CAPTURE_IDLE = 0,
	CAMERA_CAPTURE_CAPTURING,
	CAMERA_CAPTURE_READY,
	CAMERA_CAPTURE_ERROR
} CameraCaptureState_t;

//图像采集对象
typedef struct
{
	CameraConfig_t config;

	CameraCaptureState_t state;

	uint8_t *framebuffer;
	uint32_t frame_capacity;
	
    uint32_t frame_bytes;
    uint32_t valid_bytes;
	
	uint32_t start_tick;
	uint32_t timeout_ms;

	SysResult_t last_result;
	bool initialized;

} CameraCapture_t;

//CM7唯一的图像采集实例
static CameraCapture_t s_capture;

//实现三缓冲区

typedef enum
{
    CAMERA_DMA_SLOT_M0 = 0,
    CAMERA_DMA_SLOT_M1
} CameraDmaSlot_t;

typedef enum
{
    CAMERA_BUFFER_FREE = 0,
    CAMERA_BUFFER_DMA,
    CAMERA_BUFFER_READY,
    CAMERA_BUFFER_IN_USE
} CameraBufferState_t;

typedef struct
{
    uint8_t *data;
    uint32_t capacity;
    uint32_t sequence;
    CameraBufferState_t state;
} CameraFrameBuffer_t;

typedef struct
{
    CameraFrameBuffer_t buffers[CAMERA_STREAM_BUFFER_COUNT];

    uint8_t dma_m0_index;
    uint8_t dma_m1_index;

    uint8_t consumer_index;

    uint32_t next_sequence;
    uint32_t dropped_frames;
	
	uint32_t frame_bytes;

} CameraStream_t;



//计算固定长度图像所需的字节数
static SysResult_t CameraCapture_CalcSize(
    const CameraConfig_t *config,
    uint32_t *required_bytes)
{
	uint32_t bytes_per_pixel;
	uint32_t pixels;
	
	if (config == NULL || required_bytes == NULL)
		return SYS_ERR_INVALID_ARG;
	
	//YUV422每两个像素共享一组色度
	if (config->format == CAMERA_PIXEL_FORMAT_YUV422 &&
		(config->width & 1U) != 0U)
		return SYS_ERR_INVALID_ARG;

	if (config->width != 160U ||
		config->height != 120U)
	{
		return SYS_ERR_INVALID_ARG;
	}	
	
	switch (config->format)
	{
		case CAMERA_PIXEL_FORMAT_Y8:
			bytes_per_pixel = 1U;
			break;
		
		case CAMERA_PIXEL_FORMAT_RGB565:
		case CAMERA_PIXEL_FORMAT_YUV422:
			bytes_per_pixel = 2U;
			break;

		default:
			return SYS_ERR_INVALID_ARG;
	}	

	//先检查宽度乘高度是否溢出
	if ((uint32_t)config->width >
		UINT32_MAX / (uint32_t)config->height)
	{
		return SYS_ERR_INVALID_ARG;
	}

	pixels = (uint32_t)config->width *
			 (uint32_t)config->height;

	//再检查像素数量乘每像素字节数是否溢出
	if (pixels > UINT32_MAX / bytes_per_pixel)
	{
		return SYS_ERR_INVALID_ARG;
	}

	*required_bytes = pixels * bytes_per_pixel;
	
	return SYS_OK;
}

static SysResult_t CameraStream_Init(
    CameraStream_t *me,
    const CameraConfig_t *config)
{
    SysResult_t result;
    uint32_t frame_bytes = 0U;
    uint32_t i;
    uintptr_t buffer_start;
    uintptr_t buffer_end;

    //检查输入参数
    if ((me == NULL) || (config == NULL))
        return SYS_ERR_INVALID_ARG;

    //计算当前格式的实际帧大小
    result = CameraCapture_CalcSize(config, &frame_bytes);

    if (result != SYS_OK)
        return result;

    //检查单帧容量及DMA字节对齐
    if ((frame_bytes == 0U) ||
        (frame_bytes > CAMERA_STREAM_BUFFER_CAPACITY) ||
        ((frame_bytes % sizeof(uint32_t)) != 0U))
    {
        return SYS_ERR_INVALID_ARG;
    }

    buffer_start = (uintptr_t)CAMERA_BUFFER_BASE;

    //检查缓冲区地址对齐
    if ((buffer_start % CAMERA_STREAM_BUFFER_ALIGN) != 0U)
        return SYS_ERR_INVALID_ARG;

    if ((CAMERA_STREAM_BUFFER_CAPACITY %
         CAMERA_STREAM_BUFFER_ALIGN) != 0U)
    {
        return SYS_ERR_INVALID_ARG;
    }

    //检查三缓冲是否超出SDRAM的MPU映射范围
    buffer_end = buffer_start +
                 (CAMERA_STREAM_BUFFER_COUNT *
                  CAMERA_STREAM_BUFFER_CAPACITY);

    if (buffer_end > (uintptr_t)CAMERA_STREAM_SDRAM_END)
        return SYS_ERR_INVALID_ARG;

    //初始化三块物理缓冲区
    for (i = 0U; i < CAMERA_STREAM_BUFFER_COUNT; i++)
    {
        me->buffers[i].data =
            (uint8_t *)(buffer_start +
            (i * CAMERA_STREAM_BUFFER_CAPACITY));

        me->buffers[i].capacity =
            CAMERA_STREAM_BUFFER_CAPACITY;

        me->buffers[i].sequence = 0U;

        me->buffers[i].state = CAMERA_BUFFER_FREE;
    }

    //预留Buffer 0、1作为DMA双目标
    me->dma_m0_index = 0U;
    me->dma_m1_index = 1U;

    me->buffers[me->dma_m0_index].state =
        CAMERA_BUFFER_DMA;

    me->buffers[me->dma_m1_index].state =
        CAMERA_BUFFER_DMA;

    //尚未向Application交付图像
    me->consumer_index = CAMERA_BUFFER_INDEX_INVALID;

    //保存当前格式的有效帧长度
    me->frame_bytes = frame_bytes;

    //初始化统计信息
    me->next_sequence = 0U;
    me->dropped_frames = 0U;

    return SYS_OK;
}

//查找可替换DMA目标的缓冲区，优先FREE，其次READY
static uint8_t CameraStream_FindReplaceable(
    const CameraStream_t *me)
{
    uint32_t i;
    uint8_t ready_index = CAMERA_BUFFER_INDEX_INVALID;

    if (me == NULL)
        return CAMERA_BUFFER_INDEX_INVALID;

    //检查DMA目标索引是否合法
    if (me->dma_m0_index >= CAMERA_STREAM_BUFFER_COUNT ||
        me->dma_m1_index >= CAMERA_STREAM_BUFFER_COUNT ||
        me->dma_m0_index == me->dma_m1_index)
    {
        return CAMERA_BUFFER_INDEX_INVALID;
    }

    for (i = 0U; i < CAMERA_STREAM_BUFFER_COUNT; i++)
    {
        //不能选择已经分配给DMA的缓冲区
        if (i == me->dma_m0_index ||
            i == me->dma_m1_index)
        {
            continue;
        }

        //优先选择空闲缓冲区
        if (me->buffers[i].state == CAMERA_BUFFER_FREE)
        {
            return (uint8_t)i;
        }

        //记录可回收的READY缓冲区
        if (me->buffers[i].state == CAMERA_BUFFER_READY)
        {
            ready_index = (uint8_t)i;
        }
    }

    return ready_index;
}

static SysResult_t CameraStream_OnFrameComplete(
    CameraStream_t *me,
    CameraDmaSlot_t completed_slot
);

//初始化Media图像采集对象
SysResult_t CameraCapture_Init(const CameraConfig_t *config)
{
    uint32_t required_bytes;
    uint32_t dma_words;
    SysResult_t result;

    //检查输入参数
    if (config == NULL)
        return SYS_ERR_INVALID_ARG;

    //禁止重复初始化
    if (s_capture.initialized)
        return SYS_ERR_INVALID_STATE;

    //计算当前图像所需容量
    result = CameraCapture_CalcSize(config, &required_bytes);
    if (result != SYS_OK)
        return result;

    //检查当前SDRAM内存池容量
    if (required_bytes == 0U ||
        required_bytes > CAMERA_BUFFER_POOL_SIZE)
    {
        return SYS_ERR_INVALID_ARG;
    }

    //DMA采用32位Word传输，要求字节数能够整除4
    if ((required_bytes % sizeof(uint32_t)) != 0U)
        return SYS_ERR_INVALID_ARG;

    dma_words = required_bytes / sizeof(uint32_t);

    //检查DMA传输计数范围
    if (dma_words == 0U ||
        dma_words > CAMERA_DMA_MAX_WORDS)
    {
        return SYS_ERR_INVALID_ARG;
    }

    //检查Framebuffer起始地址对齐
    if ((CAMERA_BUFFER_BASE % sizeof(uint32_t)) != 0U)
        return SYS_ERR_INVALID_ARG;

    //保存图像配置
    s_capture.config = *config;

    //绑定Media管理的SDRAM区域
    s_capture.framebuffer = (uint8_t *)(uintptr_t)CAMERA_BUFFER_BASE;
    s_capture.frame_capacity = CAMERA_BUFFER_POOL_SIZE;

    //保存当前帧长度，尚未产生有效图像
    s_capture.frame_bytes = required_bytes;
    s_capture.valid_bytes = 0U;

    //初始化超时管理
    s_capture.start_tick = 0U;
    s_capture.timeout_ms = CAMERA_CAPTURE_TIMEOUT_MS;

    //初始化采集状态
    s_capture.state = CAMERA_CAPTURE_IDLE;
    s_capture.last_result = SYS_OK;

    //所有配置成功后才标记初始化完成
    s_capture.initialized = true;

    return SYS_OK;
}

//启动一次非阻塞图像采集
SysResult_t CameraCapture_Start(void)
{
    SysResult_t result;
    uint32_t dma_words;

    //Media必须已经初始化
    if (!s_capture.initialized)
        return SYS_ERR_INVALID_STATE;

    //错误状态必须先恢复
    if (s_capture.state == CAMERA_CAPTURE_ERROR)
        return SYS_ERR_INVALID_STATE;

    //正在采集或图像尚未释放时禁止重新启动
    if (s_capture.state != CAMERA_CAPTURE_IDLE)
        return SYS_ERR_BUSY;

    //检查Framebuffer是否有效
    if (s_capture.framebuffer == NULL ||
        s_capture.frame_bytes == 0U ||
        s_capture.frame_bytes > s_capture.frame_capacity)
    {
        return SYS_ERR_INVALID_STATE;
    }

    //检查DMA传输长度是否满足32位Word要求
    if ((s_capture.frame_bytes % sizeof(uint32_t)) != 0U)
        return SYS_ERR_INVALID_STATE;

    dma_words = s_capture.frame_bytes / sizeof(uint32_t);

    if (dma_words == 0U ||
        dma_words > CAMERA_DMA_MAX_WORDS)
    {
        return SYS_ERR_INVALID_STATE;
    }

    //新一轮采集开始，上一帧数据失效
    s_capture.valid_bytes = 0U;
    s_capture.last_result = SYS_OK;

    //记录采集开始时间
    s_capture.start_tick = Platform_TimeGetMs();

    //先标记采集中，再启动硬件
    s_capture.state = CAMERA_CAPTURE_CAPTURING;

    result = BSP_DCMI_StartSnapshot(
        (uint32_t *)s_capture.framebuffer,
        dma_words
    );

    if (result != SYS_OK)
    {
        //启动失败可能留下部分使能的硬件
        s_capture.last_result = result;
        s_capture.state = CAMERA_CAPTURE_ERROR;
        return result;
    }

    //只表示启动成功，不代表已经收到完整图像
    return SYS_OK;
}

//处理采集事件、超时和错误恢复
void CameraCapture_Process(void)
{
    BspDcmiEvents_t events = {0};

    //尚未初始化，不处理
    if (!s_capture.initialized)
        return;

    //处理之前遗留的错误状态
    if (s_capture.state == CAMERA_CAPTURE_ERROR)
    {
        (void)CameraCapture_Stop();
    }

    //只有采集中才需要检查完成事件和超时
    if (s_capture.state != CAMERA_CAPTURE_CAPTURING)
        return;

    //获取BSP中断记录的事件
    if (BSP_DCMI_TakeEvents(&events))
    {
        //错误优先，禁止交付不完整图像
        if ((events.flags & BSP_DCMI_EVENT_ERROR) != 0U ||
            events.result != SYS_OK)
        {
            s_capture.valid_bytes = 0U;
            s_capture.last_result =
                (events.result != SYS_OK) ? events.result : SYS_ERR_IO;

            s_capture.state = CAMERA_CAPTURE_ERROR;
        }
        //本次采集正常完成
        else if ((events.flags & BSP_DCMI_EVENT_FRAME) != 0U)
        {
            s_capture.valid_bytes = s_capture.frame_bytes;
            s_capture.last_result = SYS_OK;
            s_capture.state = CAMERA_CAPTURE_READY;
            return;
        }
    }

    //没有完成时检查采集超时
    if (s_capture.state == CAMERA_CAPTURE_CAPTURING)
    {
        uint32_t elapsed =
            (uint32_t)(Platform_TimeGetMs() - s_capture.start_tick);

        if (elapsed >= s_capture.timeout_ms)
        {
            s_capture.valid_bytes = 0U;
            s_capture.last_result = SYS_ERR_TIMEOUT;
            s_capture.state = CAMERA_CAPTURE_ERROR;
        }
    }

    //发生错误后停止硬件并恢复HAL状态
    if (s_capture.state == CAMERA_CAPTURE_ERROR)
    {
        (void)CameraCapture_Stop();
    }
}

//获取已经采集完成的图像
bool CameraCapture_GetFrame(CameraFrame_t *frame)
{
    //检查输出参数及初始化状态
    if (frame == NULL || !s_capture.initialized)
        return false;

    //只有READY状态才能交付图像
    if (s_capture.state != CAMERA_CAPTURE_READY)
        return false;

    //检查图像数据是否有效
    if (s_capture.framebuffer == NULL ||
        s_capture.valid_bytes == 0U ||
        s_capture.valid_bytes > s_capture.frame_capacity)
    {
        return false;
    }

    //返回图像描述信息
    frame->width = s_capture.config.width;
    frame->height = s_capture.config.height;
    frame->format = s_capture.config.format;

    frame->data = s_capture.framebuffer;
    frame->size_bytes = s_capture.valid_bytes;

    return true;
}

//归还FrameBuffer，允许下一次采集
SysResult_t CameraCapture_ReleaseFrame(void)
{
    //检查初始化状态
    if (!s_capture.initialized)
        return SYS_ERR_INVALID_STATE;

    //只有READY状态才允许释放完整帧
    if (s_capture.state != CAMERA_CAPTURE_READY)
        return SYS_ERR_INVALID_STATE;

    //当前图像不再有效
    s_capture.valid_bytes = 0U;

    //归还缓冲区，允许DMA下一次写入
    s_capture.state = CAMERA_CAPTURE_IDLE;

    return SYS_OK;
}

//停止当前采集，必要时恢复底层硬件
SysResult_t CameraCapture_Stop(void)
{
    SysResult_t result;

    //模块尚未初始化
    if (!s_capture.initialized)
        return SYS_ERR_INVALID_STATE;

    //完整图像尚未释放，不能直接回收缓冲区
    if (s_capture.state == CAMERA_CAPTURE_READY)
        return SYS_ERR_BUSY;

    //没有正在进行的采集
    if (s_capture.state == CAMERA_CAPTURE_IDLE)
        return SYS_OK;

    //仅CAPTURING和ERROR需要停止硬件
    if (s_capture.state != CAMERA_CAPTURE_CAPTURING &&
        s_capture.state != CAMERA_CAPTURE_ERROR)
    {
        return SYS_ERR_INVALID_STATE;
    }

    //由BSP负责停止DCMI、DMA及恢复HAL状态
    result = BSP_DCMI_Stop();

    if (result != SYS_OK)
    {
        //停止失败，禁止下一次采集
        s_capture.valid_bytes = 0U;
        s_capture.state = CAMERA_CAPTURE_ERROR;

        //保留先前发生的采集错误
        if (s_capture.last_result == SYS_OK)
            s_capture.last_result = result;

        return result;
    }

    //停止成功，当前缓冲区不再包含有效图像
    s_capture.valid_bytes = 0U;
    s_capture.start_tick = 0U;

    //硬件恢复完成，允许下一次采集
    s_capture.state = CAMERA_CAPTURE_IDLE;

    return SYS_OK;
}

//返回最近一次采集结果
SysResult_t CameraCapture_GetLastResult(void)
{
    if (!s_capture.initialized)
        return SYS_ERR_INVALID_STATE;

    return s_capture.last_result;
}
