#include "camera_capture.h"
#include "bsp_dcmi.h"
#include "platform_time.h"
#include <stddef.h>
#include <stdint.h>

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

//计算固定长度图像所需的字节数
static SysResult_t CameraCapture_CalcSize(
    const CameraConfig_t *config,
    uint32_t *required_bytes)
{
	uint32_t bytes_per_pixel;
	uint32_t pixels;
	
	if (config == NULL || required_bytes == NULL)
		return SYS_ERR_INVALID_ARG;
	
	if (config->width != 160U ||
		config->height != 120U)
	{
		return SYS_ERR_INVALID_ARG;
	}	
	
	switch (config->format)
	{
		case CAMERA_PIXEL_FORMAT_Y8:
			bytes_per_pixel = 1u;
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
