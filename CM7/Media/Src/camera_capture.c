#include "camera_capture.h"
#include "camera_mjpeg_fifo.h"
#include "bsp_dcmi.h"
#include "bsp_sdram.h"
#include "platform_time.h"
#include "stm32h7xx.h"
#include <stddef.h>
#include <stdint.h>

//图像缓冲区数量
#define CAMERA_STREAM_BUFFER_COUNT     7U

//单个缓冲区预留600KiB，实际帧长度仍由图像配置计算
#define CAMERA_STREAM_BUFFER_CAPACITY  (640U * 480U * 2U)

//缓冲区可用SDRAM范围的结束地址，不包含该地址
#define CAMERA_STREAM_SDRAM_END         BSP_SDRAM_END

//缓冲区起始地址按32字节对齐
#define CAMERA_STREAM_BUFFER_ALIGN     32U

//无效缓冲区索引，表示未关联缓冲区
#define CAMERA_BUFFER_INDEX_INVALID    UINT8_MAX

// 旧入口使用私有所有者标记，但占用 USB 的同一个配额
#define CAMERA_STREAM_LEGACY_OWNER      ((uint8_t)CAMERA_FRAME_CONSUMER_COUNT)

//首1KB保留，Snapshot与Stream复用其后的空间
#define CAMERA_BUFFER_OFFSET    0x00000400UL
#define CAMERA_BUFFER_BASE      (BSP_SDRAM_BASE + CAMERA_BUFFER_OFFSET)

//保留Snapshot原有63KB容量，不代表整个SDRAM业务窗口
#define CAMERA_BUFFER_POOL_SIZE (63U * 1024U)

#if (CAMERA_BUFFER_BASE % CAMERA_STREAM_BUFFER_ALIGN) != 0U || \
    (CAMERA_STREAM_BUFFER_CAPACITY % CAMERA_STREAM_BUFFER_ALIGN) != 0U || \
    (CAMERA_BUFFER_OFFSET + CAMERA_STREAM_BUFFER_COUNT * \
     CAMERA_STREAM_BUFFER_CAPACITY) > BSP_SDRAM_WINDOW || \
    (CAMERA_BUFFER_OFFSET + CAMERA_BUFFER_POOL_SIZE) > BSP_SDRAM_WINDOW
#error Camera buffers must fit the aligned SDRAM business window ciallo
#endif

#if (CAMERA_BUFFER_BASE + CAMERA_STREAM_BUFFER_COUNT * CAMERA_STREAM_BUFFER_CAPACITY) > CAMERA_MJPEG_FIFO_BASE
#error YUY2 buffers must not overlap the independent JPEG FIFO
#endif

//DMA单次传输最大Word数量
#define CAMERA_DMA_MAX_WORDS        0xFFFFU

//单帧采集超时时间，单位ms
#define CAMERA_CAPTURE_TIMEOUT_MS   100U

//JPEG只接收一次DMA窗口，预留容量不等于可传输整块600KiB
#define CAMERA_JPEG_CAPACITY_BYTES (0xFFFFU * 4U)
#define CAMERA_JPEG_TIMEOUT_MS 1000U

#if CAMERA_JPEG_CAPACITY_BYTES > CAMERA_STREAM_BUFFER_CAPACITY || \
    (CAMERA_BUFFER_OFFSET + CAMERA_JPEG_CAPACITY_BYTES) > BSP_SDRAM_WINDOW
#error JPEG receive window must fit SDRAM
#endif

typedef enum
{
    CAMERA_CAPTURE_MODE_NONE = 0,
    CAMERA_CAPTURE_MODE_SNAPSHOT,
    CAMERA_CAPTURE_MODE_STREAM

} CameraCaptureMode_t;

//采集状态
typedef enum
{
	CAMERA_CAPTURE_IDLE = 0,
	CAMERA_CAPTURE_CAPTURING,
	CAMERA_CAPTURE_READY,
	CAMERA_CAPTURE_DRAINING,
	CAMERA_CAPTURE_ERROR
} CameraCaptureState_t;

//单帧采集上下文
typedef struct
{
    uint8_t *framebuffer;
    uint32_t frame_capacity;
    uint32_t frame_bytes;
    uint32_t valid_bytes;

    //JPEG由硬件确认已写入内存的范围，文件有效长度另行解析
    uint32_t received_bytes;
    uint32_t start_tick;
    uint32_t timeout_ms;
} CameraSnapshot_t;

//DMA目标地址替换接口
typedef SysResult_t (*CameraStream_ReplaceFn)(
    BspDcmiTarget_t completed_target,
    uint32_t *new_buffer
);

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

    //JPEG每帧长度不同，YUY2仍使用stream.frame_bytes
    uint32_t valid_bytes;
    uint32_t sequence;
    CameraBufferState_t state;
    uint32_t lease_id; // 仅在 IN_USE 时有效，归还后清零
    uint8_t consumer;  // 实际借出者，旧入口与显式租约分开识别
} CameraFrameBuffer_t;

typedef struct
{

    //保存各缓冲区的地址、容量、帧序号和状态
    CameraFrameBuffer_t buffers[CAMERA_STREAM_BUFFER_COUNT];

    //DMA的M0目标对应的缓冲区索引
    uint8_t dma_m0_index;

    //DMA的M1目标对应的缓冲区索引
    uint8_t dma_m1_index;

    //最新完整帧索引，没有待取图像时为无效值
    uint8_t ready_index;

    uint32_t generation;    // 合法新实例分配，回绕前拒绝启动
    uint32_t next_lease_id; // 只在当前实例发放编号

    //帧序号计数器，每次完成后递增，丢帧也计数
    uint32_t next_sequence;

    //累计丢弃的图像帧数
    uint32_t dropped_frames;
	
	//当前配置下一帧图像的有效字节数
	uint32_t frame_bytes;

} CameraStream_t;

//统一管理单帧与连续采集上下文
typedef struct
{
    CameraConfig_t config;
    CameraCaptureMode_t mode;
    CameraCaptureState_t state;
    SysResult_t last_result;
    bool initialized;
    volatile bool raw_rotation_failed; // ISR 隔离新借出，不改动未提交的槽账本

    CameraSnapshot_t snapshot;
    CameraStream_t stream;
} CameraCapture_t;

//CM7唯一的图像采集实例
static CameraCapture_t s_capture;

//保留最近实际停止结果，初始化采集不能擦除同轮失败现场
static uint32_t s_stop_attempt_sequence;
//区分最近一次正常停止与ERROR后的保护性停止
static uint32_t s_stop_attempt_kind;
static SysResult_t s_stop_hardware_result = SYS_ERR_INVALID_STATE;

//保存Application提供的普通C通知入口
static CameraCaptureEventCallback_t s_capture_callback;

//保存通知接收端的静态上下文
static void *s_capture_callback_context;

//把BSP进展和错误转换为Media通知，不改变回调上下文
static void CameraCapture_OnBspEvent(uint32_t events, void *context);

//绑定静态采集通知接收端
SysResult_t CameraCapture_SetEventCallback(CameraCaptureEventCallback_t callback, void *context)
{

    //只能在Media初始化前绑定一次，不在采集中替换接收端
    if (callback == NULL)
        return SYS_ERR_INVALID_ARG;
    if (s_capture.initialized || s_capture_callback != NULL)
        return SYS_ERR_INVALID_STATE;

    //沿用原先先登记Media、再绑定BSP的顺序，保留初始化失败现场
    s_capture_callback_context = context;
    s_capture_callback = callback;

    //绑定仍在BSP初始化前完成，原始绑定错误交给启动流程处理
    return BSP_DCMI_SetEventCallback(CameraCapture_OnBspEvent, NULL);
}

//完整转发BSP进展和错误，帧完成通知仍走原来的Media路径
static void CameraCapture_OnBspEvent(uint32_t events, void *context)
{
    uint32_t media_events = 0U;
    (void)context;

    //BSP与Media的位定义不同，不能直接传递原始数值
    if ((events & BSP_DCMI_NOTIFY_PROGRESS) != 0U)
        media_events |= CAMERA_CAPTURE_EVENT_PROGRESS;

    //错误独立映射，允许同一次通知同时携带进展和错误
    if ((events & BSP_DCMI_NOTIFY_ERROR) != 0U)
        media_events |= CAMERA_CAPTURE_EVENT_ERROR;

    //绑定只发生在采集初始化前，回调不执行Stop或释放帧
    if (s_capture_callback != NULL)
        s_capture_callback(media_events, s_capture_callback_context);
}

//取得完整帧和丢帧计数快照
SysResult_t CameraCapture_GetStats(CameraCaptureStats_t *stats)
{
    uint32_t primask;

    if (stats == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!s_capture.initialized)
        return SYS_ERR_INVALID_STATE;

    //JPEG流的计数来自独立FIFO，单帧与YUY2不读取该对象
    if (s_capture.config.format == CAMERA_PIXEL_FORMAT_JPEG &&
        s_capture.mode == CAMERA_CAPTURE_MODE_STREAM)
    {
        CameraMjpegFifo_GetStats(&stats->completed_frames, &stats->dropped_frames);
        return SYS_OK;
    }

    //只短暂保护两个计数，不改变缓冲区状态
    primask = __get_PRIMASK();
    __disable_irq();
    stats->completed_frames = s_capture.stream.next_sequence;
    stats->dropped_frames = s_capture.stream.dropped_frames;
    __set_PRIMASK(primask);
    return SYS_OK;
}

//内部功能函数声明
//任务中校验JPEG段边界、分辨率和结束标记，成功才填写有效长度
static SysResult_t CameraJpeg_Validate(const uint8_t *data, uint32_t received,
    const CameraConfig_t *config, uint32_t *valid_bytes);

//任务中推进JPEG收尾、长度验证和超时，不在ISR扫描数据
static void CameraCapture_ProcessJpeg(void);

//启动独立FIFO并登记Stream生命周期
static SysResult_t CameraJpegStream_Start(void);

//计算当前格式的完整帧字节数
static SysResult_t CameraCapture_CalcSize(
    const CameraConfig_t *config,
    uint32_t *required_bytes);

//初始化五缓冲区地址和状态
static SysResult_t CameraStream_Init(
    CameraStream_t *me,
    const CameraConfig_t *config);

//检查DMA、READY和IN_USE索引与缓冲状态一致
static bool CameraStream_OwnershipValid(const CameraStream_t *me);

//查找可替换的FREE或READY缓冲区
static uint8_t CameraStream_FindReplaceable(
    const CameraStream_t *me);

//处理DMA完整帧，安全轮换物理缓冲区
static SysResult_t CameraStream_Rotate(
    CameraStream_t *me,
    BspDcmiTarget_t completed_target,
    CameraStream_ReplaceFn replace_target);

//BSP完整帧回调，运行于中断上下文
static SysResult_t CameraStream_OnFrameComplete(
    BspDcmiTarget_t completed_target,
    void *context);

//获取完整帧，并取得缓冲区读取权
static bool CameraStream_GetFrame(
    CameraStream_t *me,
    const CameraConfig_t *config,
    CameraFrame_t *frame);

//释放Application正在使用的图像
static SysResult_t CameraStream_ReleaseFrame(
    CameraStream_t *me);
	
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
    if (CameraConfig_IsVariableLength(config->format))
    {
        if (config->width == 0U || config->height == 0U)
            return SYS_ERR_INVALID_ARG;
        required_bytes = CAMERA_JPEG_CAPACITY_BYTES;
    }
    else
    {
        result = CameraCapture_CalcSize(config, &required_bytes);
        if (result != SYS_OK)
            return result;
    }

    //检查当前SDRAM内存池容量
    if (required_bytes == 0U ||
        required_bytes > (CameraConfig_IsVariableLength(config->format) ?
            CAMERA_JPEG_CAPACITY_BYTES : CAMERA_BUFFER_POOL_SIZE))
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
    s_capture.snapshot.framebuffer = (uint8_t *)(uintptr_t)CAMERA_BUFFER_BASE;
    s_capture.snapshot.frame_capacity = CameraConfig_IsVariableLength(config->format) ?
        CAMERA_JPEG_CAPACITY_BYTES : CAMERA_BUFFER_POOL_SIZE;

    //保存当前帧长度，尚未产生有效图像
    s_capture.snapshot.frame_bytes = CameraConfig_IsVariableLength(config->format) ?
        0U : required_bytes;
    s_capture.snapshot.received_bytes = 0U;
    s_capture.snapshot.valid_bytes = 0U;

    //初始化超时管理
    s_capture.snapshot.start_tick = 0U;
    s_capture.snapshot.timeout_ms = CameraConfig_IsVariableLength(config->format) ?
        CAMERA_JPEG_TIMEOUT_MS : CAMERA_CAPTURE_TIMEOUT_MS;

    //初始化采集状态
    s_capture.mode = CAMERA_CAPTURE_MODE_NONE;
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
    if (s_capture.mode != CAMERA_CAPTURE_MODE_NONE ||
        s_capture.state != CAMERA_CAPTURE_IDLE)
        return SYS_ERR_BUSY;

    //检查Framebuffer是否有效
    if (s_capture.snapshot.framebuffer == NULL ||
        s_capture.snapshot.frame_capacity == 0U ||
        (!CameraConfig_IsVariableLength(s_capture.config.format) &&
         (s_capture.snapshot.frame_bytes == 0U ||
          s_capture.snapshot.frame_bytes > s_capture.snapshot.frame_capacity)))
    {
        return SYS_ERR_INVALID_STATE;
    }

    //检查DMA传输长度是否满足32位Word要求
    if ((s_capture.snapshot.frame_bytes % sizeof(uint32_t)) != 0U ||
        (CameraConfig_IsVariableLength(s_capture.config.format) &&
         s_capture.snapshot.frame_capacity % sizeof(uint32_t) != 0U))
        return SYS_ERR_INVALID_STATE;

    dma_words = (CameraConfig_IsVariableLength(s_capture.config.format) ?
        s_capture.snapshot.frame_capacity : s_capture.snapshot.frame_bytes) / sizeof(uint32_t);

    if (dma_words == 0U ||
        dma_words > CAMERA_DMA_MAX_WORDS)
    {
        return SYS_ERR_INVALID_STATE;
    }

    //新一轮采集开始，上一帧数据失效
    s_capture.snapshot.valid_bytes = 0U;
    s_capture.snapshot.received_bytes = 0U;
    s_capture.last_result = SYS_OK;

    //记录采集开始时间
    s_capture.snapshot.start_tick = Platform_TimeGetMs();

    //先标记采集中，再启动硬件
    s_capture.state = CAMERA_CAPTURE_CAPTURING;

	s_capture.mode = CAMERA_CAPTURE_MODE_SNAPSHOT;
    result = CameraConfig_IsVariableLength(s_capture.config.format) ?
        BSP_DCMI_StartJpegSnapshot((uint32_t *)s_capture.snapshot.framebuffer, dma_words) :
        BSP_DCMI_StartSnapshot(
        (uint32_t *)s_capture.snapshot.framebuffer,
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

//启动连续采集
SysResult_t CameraCapture_StartStream(void)
{
    SysResult_t result;
    CameraStream_t *stream = &s_capture.stream;
    uint32_t dma_words;

    //1. 检查Media初始化
    if (!s_capture.initialized)
        return SYS_ERR_INVALID_STATE;

    //故障恢复后才能重新启动
    if (s_capture.state == CAMERA_CAPTURE_ERROR)
        return SYS_ERR_INVALID_STATE;

    //2. 防止Snapshot与Stream同时运行
    if (s_capture.mode != CAMERA_CAPTURE_MODE_NONE ||
        s_capture.state != CAMERA_CAPTURE_IDLE)
    {
        return SYS_ERR_BUSY;
    }

    //JPEG连续流使用独立字节FIFO，不借用Snapshot或YUY2五缓冲
    if (CameraConfig_IsVariableLength(s_capture.config.format))
        return CameraJpegStream_Start();

    //3. 初始化五缓冲状态
    result = CameraStream_Init(
        stream,
        &s_capture.config
    );

    if (result != SYS_OK)
        return result;

    //4. 单帧字节数转换为DMA Word数量
    dma_words = stream->frame_bytes / sizeof(uint32_t);
    s_capture.raw_rotation_failed = false;

    //5. 启动前先占用Stream模式和采集状态
    s_capture.mode = CAMERA_CAPTURE_MODE_STREAM;
    s_capture.state = CAMERA_CAPTURE_CAPTURING;

    //6. 注册完整帧回调并启动DMA DBM
    result = BSP_DCMI_StartContinuous(
        (uint32_t *)stream->buffers[
            stream->dma_m0_index
        ].data,

        (uint32_t *)stream->buffers[
            stream->dma_m1_index
        ].data,

        dma_words,

        CameraStream_OnFrameComplete,

        stream
    );

    if (result != SYS_OK)
    {
        s_capture.last_result = result;
        s_capture.state = CAMERA_CAPTURE_ERROR;

        //BSP可能已部分启动
        //保留模式占用，后续必须通过Stop恢复
        return result;
    }

    s_capture.last_result = SYS_OK;

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

    //JPEG Snapshot保持已验证的单帧收尾，Stream不再调用单帧接口
    if (CameraConfig_IsVariableLength(s_capture.config.format))
    {
        if (s_capture.mode == CAMERA_CAPTURE_MODE_SNAPSHOT)
            CameraCapture_ProcessJpeg();
        else
        {
            SysResult_t result = SYS_OK;

            //硬件错误优先，地址替换失败的完成块不能发布
            if (BSP_DCMI_TakeEvents(&events) &&
                ((events.flags & BSP_DCMI_EVENT_ERROR) != 0U || events.result != SYS_OK))
                result = events.result != SYS_OK ? events.result : SYS_ERR_IO;
            if (result == SYS_OK)
                result = CameraMjpegFifo_Process();

            //解析期间也可能发生IRQ错误，交付前再次确认
            if (BSP_DCMI_TakeEvents(&events) &&
                ((events.flags & BSP_DCMI_EVENT_ERROR) != 0U || events.result != SYS_OK))
                result = events.result != SYS_OK ? events.result : SYS_ERR_IO;
            if (result != SYS_OK)
            {
                s_capture.last_result = result;
                s_capture.state = CAMERA_CAPTURE_ERROR;
                (void)CameraCapture_Stop();
            }
        }
        return;
    }

    //获取BSP中断记录的事件
    if (BSP_DCMI_TakeEvents(&events))
    {

        //错误优先，禁止交付不完整图像
        if ((events.flags & BSP_DCMI_EVENT_ERROR) != 0U ||
            events.result != SYS_OK)
        {
            s_capture.snapshot.valid_bytes = 0U;
            s_capture.last_result =
                (events.result != SYS_OK) ? events.result : SYS_ERR_IO;

            s_capture.state = CAMERA_CAPTURE_ERROR;
        }

        //本次采集正常完成
        else if (s_capture.mode == CAMERA_CAPTURE_MODE_SNAPSHOT &&
                 (events.flags & BSP_DCMI_EVENT_FRAME) != 0U)
        {
            s_capture.snapshot.valid_bytes = s_capture.snapshot.frame_bytes;
            s_capture.last_result = SYS_OK;
            s_capture.state = CAMERA_CAPTURE_READY;
            return;
        }
    }

    //只有Snapshot使用单帧采集超时
    if (s_capture.mode == CAMERA_CAPTURE_MODE_SNAPSHOT &&
        s_capture.state == CAMERA_CAPTURE_CAPTURING)
    {
        uint32_t elapsed =
            (uint32_t)(Platform_TimeGetMs() - s_capture.snapshot.start_tick);

        if (elapsed >= s_capture.snapshot.timeout_ms)
        {
            s_capture.snapshot.valid_bytes = 0U;
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
    if (s_capture.mode != CAMERA_CAPTURE_MODE_SNAPSHOT ||
        s_capture.state != CAMERA_CAPTURE_READY)
        return false;

    //检查图像数据是否有效
    if (s_capture.snapshot.framebuffer == NULL ||
        s_capture.snapshot.valid_bytes == 0U ||
        s_capture.snapshot.valid_bytes > s_capture.snapshot.frame_capacity)
    {
        return false;
    }

    //返回图像描述信息
    frame->width = s_capture.config.width;
    frame->height = s_capture.config.height;
    frame->format = s_capture.config.format;

    frame->data = s_capture.snapshot.framebuffer;
    frame->size_bytes = s_capture.snapshot.valid_bytes;
    frame->data2 = NULL;
    frame->size_bytes2 = 0U;

    return true;
}

//归还FrameBuffer，允许下一次采集
SysResult_t CameraCapture_ReleaseFrame(void)
{

    //检查初始化状态
    if (!s_capture.initialized)
        return SYS_ERR_INVALID_STATE;

    //只有READY状态才允许释放完整帧
    if (s_capture.mode != CAMERA_CAPTURE_MODE_SNAPSHOT ||
        s_capture.state != CAMERA_CAPTURE_READY)
        return SYS_ERR_INVALID_STATE;

	SysResult_t result;

	s_stop_attempt_kind = s_capture.state == CAMERA_CAPTURE_ERROR ?
        CAMERA_STOP_PROTECTIVE_ATTEMPT : CAMERA_STOP_NORMAL_ATTEMPT;
    result = BSP_DCMI_Stop();
    //只记录实际底层调用，BUSY排空检查不伪造新的硬件尝试
    ++s_stop_attempt_sequence;
    s_stop_hardware_result = result;

	if (result != SYS_OK)
	{
		s_capture.last_result = result;
		s_capture.state = CAMERA_CAPTURE_ERROR;
		s_capture.snapshot.valid_bytes = 0U;
		return result;
	}
	
    //当前图像不再有效
    s_capture.snapshot.valid_bytes = 0U;

    //归还缓冲区，允许DMA下一次写入
    s_capture.mode = CAMERA_CAPTURE_MODE_NONE;
    s_capture.state = CAMERA_CAPTURE_IDLE;

    return SYS_OK;
}

// 只查原物理槽；DCMI 完成中断不会创建或释放活动租约
static bool CameraCapture_StreamHasLoan(void)
{
    uint32_t index;

    if (s_capture.config.format == CAMERA_PIXEL_FORMAT_JPEG)
        return CameraMjpegFifo_HasLoan();

    for (index = 0U; index < CAMERA_STREAM_BUFFER_COUNT; ++index)
        if (s_capture.stream.buffers[index].state == CAMERA_BUFFER_IN_USE)
            return true;
    return false;
}

//停止采集并恢复硬件，保留未释放缓冲区的所有权
SysResult_t CameraCapture_Stop(void)
{
    SysResult_t result;
    bool has_in_use = false;

    //1. 检查初始化与模式
    if (!s_capture.initialized)
        return SYS_ERR_INVALID_STATE;

    if (s_capture.mode == CAMERA_CAPTURE_MODE_NONE)
    {
        return (s_capture.state == CAMERA_CAPTURE_IDLE)
            ? SYS_OK : SYS_ERR_INVALID_STATE;
    }

    if (s_capture.mode != CAMERA_CAPTURE_MODE_SNAPSHOT &&
        s_capture.mode != CAMERA_CAPTURE_MODE_STREAM)
        return SYS_ERR_INVALID_STATE;

    //2. 已停止硬件，正在等待消费者归还缓冲区
    if (s_capture.state == CAMERA_CAPTURE_DRAINING)
    {
        //硬件已停止，最后一张SD租借归还后才能结束JPEG Stream
        if (s_capture.mode == CAMERA_CAPTURE_MODE_STREAM && !CameraCapture_StreamHasLoan())
        {
            s_capture.mode = CAMERA_CAPTURE_MODE_NONE;
            s_capture.state = CAMERA_CAPTURE_IDLE;
            return SYS_OK;
        }
        return (s_capture.mode == CAMERA_CAPTURE_MODE_STREAM)
            ? SYS_ERR_BUSY : SYS_ERR_INVALID_STATE;
    }

    //Snapshot完整帧应通过ReleaseFrame释放
    if (s_capture.state == CAMERA_CAPTURE_READY)
        return SYS_ERR_BUSY;

    if (s_capture.state != CAMERA_CAPTURE_CAPTURING &&
        s_capture.state != CAMERA_CAPTURE_ERROR)
        return SYS_ERR_INVALID_STATE;

    //3. 检查Stream是否有Application持有的缓冲区
    if (s_capture.mode == CAMERA_CAPTURE_MODE_STREAM)
    {
        has_in_use = CameraCapture_StreamHasLoan();

        //正常主动停止：先要求Application释放图像
        if (s_capture.state == CAMERA_CAPTURE_CAPTURING &&
            has_in_use)
        {
            return SYS_ERR_BUSY;
        }

        //ERROR状态不在此处拒绝，必须尝试停止硬件
    }

    //4. 停止DCMI和DMA
    s_stop_attempt_kind = s_capture.state == CAMERA_CAPTURE_ERROR ?
        CAMERA_STOP_PROTECTIVE_ATTEMPT : CAMERA_STOP_NORMAL_ATTEMPT;
    result = BSP_DCMI_Stop();
    //只记录实际底层调用，BUSY排空检查不伪造新的硬件尝试
    ++s_stop_attempt_sequence;
    s_stop_hardware_result = result;

    if (result != SYS_OK)
    {
        s_capture.state = CAMERA_CAPTURE_ERROR;

        //保留最早的故障原因
        if (s_capture.last_result == SYS_OK)
            s_capture.last_result = result;

        return result;
    }

    //只在硬件停止确认成功后释放FIFO接收区，外部借用继续保留
    if (s_capture.mode == CAMERA_CAPTURE_MODE_STREAM &&
        s_capture.config.format == CAMERA_PIXEL_FORMAT_JPEG)
        CameraMjpegFifo_OnStopped();

    //5. Snapshot清理
    if (s_capture.mode == CAMERA_CAPTURE_MODE_SNAPSHOT)
    {
        s_capture.snapshot.valid_bytes = 0U;
        s_capture.snapshot.start_tick = 0U;
    }

    //6. Stream故障停止后仍有IN_USE
    if (s_capture.mode == CAMERA_CAPTURE_MODE_STREAM &&
        has_in_use)
    {

        //硬件已停止，但Buffer仍归Application所有
        s_capture.state = CAMERA_CAPTURE_DRAINING;

        return SYS_OK;
    }

    //7. 没有消费者持有Buffer，彻底释放采集模式
    s_capture.mode = CAMERA_CAPTURE_MODE_NONE;
    s_capture.state = CAMERA_CAPTURE_IDLE;

    return SYS_OK;
}

//只查询完整停止条件，不改变采集和缓冲区状态
bool CameraCapture_IsStopped(void)
{
    return s_capture.initialized &&
        s_capture.mode == CAMERA_CAPTURE_MODE_NONE &&
        s_capture.state == CAMERA_CAPTURE_IDLE;
}

//读取本地停止事实，不把历史失败改写为后续排空成功
SysResult_t CameraCapture_GetStopFacts(CameraCaptureStopFacts_t *facts)
{
    if (facts == NULL)
        return SYS_ERR_INVALID_ARG;
    facts->attempt_sequence = s_stop_attempt_sequence;
    facts->hardware_result = s_stop_hardware_result;
    facts->flags = s_stop_attempt_kind | (s_capture.initialized ? CAMERA_STOP_INITIALIZED : 0U);
    if (s_capture.initialized && s_capture.state == CAMERA_CAPTURE_ERROR)
        facts->flags |= CAMERA_STOP_ERROR;
    if (s_capture.initialized && s_capture.state == CAMERA_CAPTURE_DRAINING)
        facts->flags |= CAMERA_STOP_DRAINING;
    if (CameraCapture_IsStopped())
        facts->flags |= CAMERA_STOP_COMPLETE;
    return SYS_OK;
}

//返回最近一次采集结果
SysResult_t CameraCapture_GetLastResult(void)
{
    if (!s_capture.initialized)
        return SYS_ERR_INVALID_STATE;

    return s_capture.last_result;
}

//内部功能函数实现
//计算固定长度图像所需的字节数
static SysResult_t CameraCapture_CalcSize(
    const CameraConfig_t *config,
    uint32_t *required_bytes)
{
    return CameraConfig_GetFrameSize(config, required_bytes);
}

SysResult_t CameraCapture_ValidateStreamConfig(const CameraConfig_t *config)
{
    uint32_t bytes;
    SysResult_t result;

    //先拒绝空配置，不能访问无效指针
    if (config == NULL)
        return SYS_ERR_INVALID_ARG;

    //JPEG流采用变长FIFO，只开放当前已配置的分辨率
    if (config->format == CAMERA_PIXEL_FORMAT_JPEG)
    {
        if (config->width == 0U || config->height == 0U)
            return SYS_ERR_INVALID_ARG;
        if (!CameraConfig_IsJpegSizeSupported(config))
            return SYS_ERR_NOT_SUPPORTED;
        return SYS_OK;
    }

    //固定长度格式仍按原五缓冲和单次DMA计数校验
    result = CameraCapture_CalcSize(config, &bytes);

    if (result != SYS_OK)
        return result;

    if (bytes > CAMERA_STREAM_BUFFER_CAPACITY || (bytes % 4U) != 0U ||
        bytes / 4U > CAMERA_DMA_MAX_WORDS)
        return SYS_ERR_NOT_SUPPORTED;

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

    //检查五缓冲是否超出SDRAM的MPU映射范围
    buffer_end = buffer_start +
                 (CAMERA_STREAM_BUFFER_COUNT *
                  CAMERA_STREAM_BUFFER_CAPACITY);

    if (buffer_end > (uintptr_t)CAMERA_STREAM_SDRAM_END)
        return SYS_ERR_INVALID_ARG;

    // 在改写槽布局前拒绝耗尽的实例编号，旧凭据不能跨实例复用
    if (me->generation == UINT32_MAX)
        return SYS_ERR_NO_RESOURCE;

    //初始化三块物理缓冲区
    for (i = 0U; i < CAMERA_STREAM_BUFFER_COUNT; i++)
    {
        me->buffers[i].data =
            (uint8_t *)(buffer_start +
            (i * CAMERA_STREAM_BUFFER_CAPACITY));

        me->buffers[i].capacity =
            CAMERA_STREAM_BUFFER_CAPACITY;

        me->buffers[i].sequence = 0U;
        me->buffers[i].lease_id = 0U;
        me->buffers[i].consumer = 0U;

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
    me->ready_index = CAMERA_BUFFER_INDEX_INVALID;
    ++me->generation;
    me->next_lease_id = 0U;

    //保存当前格式的有效帧长度
    me->frame_bytes = frame_bytes;

    //初始化统计信息
    me->next_sequence = 0U;
    me->dropped_frames = 0U;

    return SYS_OK;
}

// 从物理槽验证全部所有权：两块 DMA 不重复，每个消费者最多占用一槽
static bool CameraStream_OwnershipValid(const CameraStream_t *stream)
{
    uint32_t index, borrowers = 0U, spare_count = 0U;

    if (stream == NULL || stream->generation == 0U ||
        stream->dma_m0_index >= CAMERA_STREAM_BUFFER_COUNT ||
        stream->dma_m1_index >= CAMERA_STREAM_BUFFER_COUNT ||
        stream->dma_m0_index == stream->dma_m1_index)
        return false;

    // 最新帧提示可以为空；非空时必须仍指向未借出的真实 READY
    if (stream->ready_index != CAMERA_BUFFER_INDEX_INVALID &&
        (stream->ready_index >= CAMERA_STREAM_BUFFER_COUNT ||
         stream->buffers[stream->ready_index].state != CAMERA_BUFFER_READY))
        return false;

    for (index = 0U; index < CAMERA_STREAM_BUFFER_COUNT; ++index)
    {
        const CameraFrameBuffer_t *buffer = &stream->buffers[index];

        // DMA 目标必须与物理状态一致，且不能携带借用身份
        if (index == stream->dma_m0_index || index == stream->dma_m1_index)
        {
            if (buffer->state != CAMERA_BUFFER_DMA || buffer->lease_id != 0U)
                return false;
            continue;
        }

        // FREE 和未借出的 READY 都可以作为下一次轮换目标
        if (buffer->state == CAMERA_BUFFER_FREE || buffer->state == CAMERA_BUFFER_READY)
        {
            if (buffer->lease_id != 0U)
                return false;
            ++spare_count;
            continue;
        }

        // 非 DMA、非候选槽必须是带有效租借身份的 IN_USE
        if (buffer->state != CAMERA_BUFFER_IN_USE || buffer->lease_id == 0U ||
            buffer->lease_id > stream->next_lease_id || buffer->consumer > CAMERA_STREAM_LEGACY_OWNER)
            return false;

        // 旧 API 占用 USB 配额，但其私有身份不能被显式 Release 冒用
        const uint32_t consumer = buffer->consumer == CAMERA_STREAM_LEGACY_OWNER ?
            CAMERA_FRAME_CONSUMER_USB : buffer->consumer;
        const uint32_t bit = 1UL << consumer;
        if ((borrowers & bit) != 0U)
            return false;
        borrowers |= bit;
    }
    return spare_count != 0U;
}

//优先使用多余FREE，只有一个FREE时回收最旧的未借出READY
static uint8_t CameraStream_FindReplaceable(const CameraStream_t *me)
{
    uint32_t i, free_count = 0U, age, oldest_age = 0U;
    uint8_t free_index = CAMERA_BUFFER_INDEX_INVALID;
    uint8_t ready_index = CAMERA_BUFFER_INDEX_INVALID;
    if (me == NULL)
        return CAMERA_BUFFER_INDEX_INVALID;
    for (i = 0U; i < CAMERA_STREAM_BUFFER_COUNT; i++)
    {
        if (i == me->dma_m0_index || i == me->dma_m1_index)
            continue;
        if (me->buffers[i].state == CAMERA_BUFFER_FREE)
        {
            if (free_index == CAMERA_BUFFER_INDEX_INVALID)
                free_index = (uint8_t)i;
            free_count++;
        }
        else if (me->buffers[i].state == CAMERA_BUFFER_READY)
        {
            //无符号帧龄支持计数回绕，正常在池内的帧龄保持有界
            age = me->next_sequence - me->buffers[i].sequence;
            if (ready_index == CAMERA_BUFFER_INDEX_INVALID || age > oldest_age)
            {
                oldest_age = age;
                ready_index = (uint8_t)i;
            }
        }
    }
    // 满四个租约时只剩一块可轮换，唯一 FREE 同样合法
    return free_count > 1U || ready_index == CAMERA_BUFFER_INDEX_INVALID ? free_index : ready_index;
}

//处理DMA完整帧并保留多个READY，只向消费者交付最新帧
//处理DMA完整帧，安全轮换物理缓冲区
static SysResult_t CameraStream_Rotate(
    CameraStream_t *me,
    BspDcmiTarget_t completed_target,
    CameraStream_ReplaceFn replace_target)
{
    uint8_t completed_index;
    uint8_t replacement_index;
    bool drops_ready;
    SysResult_t result;

    if (me == NULL || replace_target == NULL)
        return SYS_ERR_INVALID_ARG;
    if (completed_target != BSP_DCMI_TARGET_M0 &&
        completed_target != BSP_DCMI_TARGET_M1)
        return SYS_ERR_INVALID_ARG;

    //先核对全部所有权，允许多个READY，但拒绝DMA或借出索引失配
    if (!CameraStream_OwnershipValid(me))
        return SYS_ERR_INVALID_STATE;

    completed_index = completed_target == BSP_DCMI_TARGET_M0 ?
        me->dma_m0_index : me->dma_m1_index;
    replacement_index = CameraStream_FindReplaceable(me);
    if (replacement_index == CAMERA_BUFFER_INDEX_INVALID)
        return SYS_ERR_INVALID_STATE;

    //容量与实际帧长分开检查，不把预留容量作为DMA传输长度
    if (me->buffers[replacement_index].data == NULL ||
        me->frame_bytes == 0U ||
        me->frame_bytes > me->buffers[replacement_index].capacity ||
        me->frame_bytes % sizeof(uint32_t) != 0U)
        return SYS_ERR_INVALID_STATE;

    //BSP确认硬件不再使用完成目标后，才能提交软件所有权
    drops_ready = me->buffers[replacement_index].state == CAMERA_BUFFER_READY;
    result = replace_target(completed_target,
        (uint32_t *)me->buffers[replacement_index].data);
    if (result != SYS_OK)
        return result;

    //旧READY只在真正被回收时计丢帧，不触碰IN_USE
    if (drops_ready)
        me->dropped_frames++;

    me->buffers[replacement_index].state = CAMERA_BUFFER_DMA;
    me->buffers[completed_index].state = CAMERA_BUFFER_READY;
    me->buffers[completed_index].sequence = ++me->next_sequence;
    if (completed_target == BSP_DCMI_TARGET_M0)
        me->dma_m0_index = replacement_index;
    else
        me->dma_m1_index = replacement_index;
    me->ready_index = completed_index;
    return SYS_OK;
}

// 查找当前可借出的最新 READY；旧接口仅使用原有的最新帧提示
static uint8_t CameraStream_FindReady(const CameraStream_t *stream, uint8_t consumer)
{
    uint8_t latest = stream->ready_index;
    uint32_t index, youngest_age = UINT32_MAX;

    if (latest != CAMERA_BUFFER_INDEX_INVALID || consumer == CAMERA_STREAM_LEGACY_OWNER)
        return latest;

    // 最新帧已交付时，显式消费者可独立取得下一块尚未借出的 READY
    for (index = 0U; index < CAMERA_STREAM_BUFFER_COUNT; ++index)
    {
        const CameraFrameBuffer_t *buffer = &stream->buffers[index];
        uint32_t age = stream->next_sequence - buffer->sequence;
        if (buffer->state == CAMERA_BUFFER_READY &&
            (latest == CAMERA_BUFFER_INDEX_INVALID || age < youngest_age))
        {
            latest = (uint8_t)index;
            youngest_age = age;
        }
    }
    return latest;
}

// 按实际物理槽检查配额；旧调用与显式 USB 共享一个消费者配额
static bool CameraStream_ConsumerHasLoan(const CameraStream_t *stream, uint8_t consumer)
{
    uint32_t index;
    bool usb_quota = consumer == CAMERA_FRAME_CONSUMER_USB || consumer == CAMERA_STREAM_LEGACY_OWNER;

    for (index = 0U; index < CAMERA_STREAM_BUFFER_COUNT; ++index)
    {
        const CameraFrameBuffer_t *buffer = &stream->buffers[index];
        if (buffer->state == CAMERA_BUFFER_IN_USE &&
            (buffer->consumer == consumer || (usb_quota &&
             (buffer->consumer == CAMERA_FRAME_CONSUMER_USB ||
              buffer->consumer == CAMERA_STREAM_LEGACY_OWNER))))
            return true;
    }
    return false;
}

// 在同一个短临界区选择 READY 并提交槽身份，完成中断不能抢走该槽
static SysResult_t CameraStream_Acquire(CameraStream_t *stream, const CameraConfig_t *config,
    uint8_t consumer, CameraFrameLease_t *lease, CameraFrame_t *frame)
{
    uint32_t primask;
    uint8_t index;
    CameraFrameBuffer_t *buffer;
    SysResult_t result = SYS_ERR_INVALID_STATE;

    if (stream == NULL || config == NULL || lease == NULL || frame == NULL)
        return SYS_ERR_INVALID_ARG;

    primask = __get_PRIMASK();
    __disable_irq();
    // 换址不确定时冻结所有新借出，但已经发出的身份仍可归还
    if ((stream == &s_capture.stream && s_capture.raw_rotation_failed) ||
        !CameraStream_OwnershipValid(stream))
        goto done;

    index = CameraStream_FindReady(stream, consumer);
    if (index == CAMERA_BUFFER_INDEX_INVALID || CameraStream_ConsumerHasLoan(stream, consumer))
    {
        result = SYS_ERR_BUSY;
        goto done;
    }

    // 不发出回绕后的编号，避免旧凭据命中复用槽
    if (stream->next_lease_id == UINT32_MAX)
    {
        result = SYS_ERR_NO_RESOURCE;
        goto done;
    }

    buffer = &stream->buffers[index];
    if (buffer->data == NULL || stream->frame_bytes == 0U || stream->frame_bytes > buffer->capacity)
        goto done;

    // 只从真实 READY 转成 IN_USE，失败路径不改写两个输出
    buffer->state = CAMERA_BUFFER_IN_USE;
    buffer->consumer = consumer;
    buffer->lease_id = ++stream->next_lease_id;
    if (stream->ready_index == index)
        stream->ready_index = CAMERA_BUFFER_INDEX_INVALID;

    *lease = (CameraFrameLease_t){stream->generation, buffer->lease_id, index,
        (CameraFrameConsumer_t)consumer};
    *frame = (CameraFrame_t){
        .width = config->width, .height = config->height, .format = config->format,
        .data = buffer->data, .size_bytes = stream->frame_bytes, .data2 = NULL, .size_bytes2 = 0U
    };
    result = SYS_OK;

done:
    __set_PRIMASK(primask);
    return result;
}

// 旧入口保留单帧自检。借用时共享 USB 配额，释放时只认可旧入口建立的身份
static bool CameraStream_GetFrame(CameraStream_t *stream, const CameraConfig_t *config,
    CameraFrame_t *frame)
{
    CameraFrameLease_t lease;

    return CameraStream_Acquire(stream, config, CAMERA_STREAM_LEGACY_OWNER, &lease, frame) == SYS_OK;
}

bool CameraCapture_StreamGetFrame(CameraFrame_t *frame)
{
    if (!s_capture.initialized ||
        s_capture.mode != CAMERA_CAPTURE_MODE_STREAM ||
        s_capture.state != CAMERA_CAPTURE_CAPTURING)
    {
        return false;
    }

    if (s_capture.config.format == CAMERA_PIXEL_FORMAT_JPEG)
        return CameraMjpegFifo_GetFrame(frame);

    return CameraStream_GetFrame(
        &s_capture.stream,
        &s_capture.config,
        frame
    );
}

// IRQ 已屏蔽；核对槽、实例、租约编号和实际所有者后才归还
static SysResult_t CameraStream_Return(CameraStream_t *stream, const CameraFrameLease_t *lease)
{
    CameraFrameBuffer_t *buffer;

    if (!CameraStream_OwnershipValid(stream) || lease->buffer_id >= CAMERA_STREAM_BUFFER_COUNT ||
        lease->generation != stream->generation)
        return SYS_ERR_INVALID_STATE;

    buffer = &stream->buffers[lease->buffer_id];
    if (buffer->state != CAMERA_BUFFER_IN_USE || buffer->lease_id != lease->lease_id ||
        buffer->consumer != (uint8_t)lease->consumer)
        return SYS_ERR_INVALID_STATE;

    // 先解除租借身份，之后 DMA 才能将此槽视为可替换
    buffer->lease_id = 0U;
    buffer->consumer = 0U;
    buffer->state = CAMERA_BUFFER_FREE;
    return SYS_OK;
}

static SysResult_t CameraStream_ReleaseFrame(CameraStream_t *stream)
{
    uint32_t primask, index;
    SysResult_t result = SYS_ERR_INVALID_STATE;

    if (stream == NULL)
        return SYS_ERR_INVALID_ARG;

    // 旧接口没有身份参数，只在原账本寻找旧入口创建的独占槽
    primask = __get_PRIMASK();
    __disable_irq();
    for (index = 0U; index < CAMERA_STREAM_BUFFER_COUNT; ++index)
    {
        const CameraFrameBuffer_t *buffer = &stream->buffers[index];
        if (buffer->state == CAMERA_BUFFER_IN_USE && buffer->consumer == CAMERA_STREAM_LEGACY_OWNER)
        {
            const CameraFrameLease_t lease = {stream->generation, buffer->lease_id, index,
                (CameraFrameConsumer_t)CAMERA_STREAM_LEGACY_OWNER};
            result = CameraStream_Return(stream, &lease);
            break;
        }
    }
    __set_PRIMASK(primask);
    return result;
}

// 归还在正常采集、错误和硬件已停止的排空阶段均有效
static bool CameraCapture_CanReturnStream(void)
{
    return s_capture.initialized && s_capture.mode == CAMERA_CAPTURE_MODE_STREAM &&
        (s_capture.state == CAMERA_CAPTURE_CAPTURING ||
         s_capture.state == CAMERA_CAPTURE_ERROR || s_capture.state == CAMERA_CAPTURE_DRAINING);
}

static void CameraCapture_FinishStreamDrain(void)
{
    // 只有已经停下硬件，并且最后一块物理借用归还，才允许下一实例启动
    if (s_capture.state == CAMERA_CAPTURE_DRAINING && !CameraCapture_StreamHasLoan())
    {
        s_capture.mode = CAMERA_CAPTURE_MODE_NONE;
        s_capture.state = CAMERA_CAPTURE_IDLE;
    }
}

SysResult_t CameraCapture_StreamReleaseFrame(void)
{
    SysResult_t result;

    if (!CameraCapture_CanReturnStream())
        return SYS_ERR_INVALID_STATE;
    // JPEG 保留 FIFO 原有释放契约
    result = s_capture.config.format == CAMERA_PIXEL_FORMAT_JPEG ?
        CameraMjpegFifo_ReleaseFrame() : CameraStream_ReleaseFrame(&s_capture.stream);
    if (result == SYS_OK)
        CameraCapture_FinishStreamDrain();
    return result;
}

SysResult_t CameraCapture_StreamAcquire(CameraFrameConsumer_t consumer,
    CameraFrameLease_t *lease, CameraFrame_t *frame)
{
    // 拒绝未知身份；只有 ACTIVE 的 Raw 采集实例能发放租约
    if ((uint32_t)consumer >= CAMERA_FRAME_CONSUMER_COUNT || lease == NULL || frame == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!s_capture.initialized || s_capture.mode != CAMERA_CAPTURE_MODE_STREAM ||
        s_capture.state != CAMERA_CAPTURE_CAPTURING)
        return SYS_ERR_INVALID_STATE;
    if (s_capture.config.format == CAMERA_PIXEL_FORMAT_JPEG)
        return SYS_ERR_NOT_SUPPORTED;

    return CameraStream_Acquire(&s_capture.stream, &s_capture.config, (uint8_t)consumer, lease, frame);
}

//录像从JPEG FIFO取得SD位，与Raw的物理账本完全独立。
SysResult_t CameraCapture_StreamAcquireJpegStorage(CameraMjpegLease_t *lease, CameraFrame_t *frame)
{
    if (!s_capture.initialized || s_capture.mode != CAMERA_CAPTURE_MODE_STREAM ||
        s_capture.state != CAMERA_CAPTURE_CAPTURING)
        return SYS_ERR_INVALID_STATE;
    if (s_capture.config.format != CAMERA_PIXEL_FORMAT_JPEG)
        return SYS_ERR_NOT_SUPPORTED;
    return CameraMjpegFifo_AcquireStorage(lease, frame);
}

//保留原错误排空转换，最后一个真实借用归还后才离开DRAINING。
SysResult_t CameraCapture_StreamReleaseJpegStorage(const CameraMjpegLease_t *lease)
{
    SysResult_t result;

    if (!CameraCapture_CanReturnStream() || s_capture.config.format != CAMERA_PIXEL_FORMAT_JPEG)
        return SYS_ERR_INVALID_STATE;
    result = CameraMjpegFifo_ReleaseLease(lease);
    if (result == SYS_OK)
        CameraCapture_FinishStreamDrain();
    return result;
}

SysResult_t CameraCapture_StreamReleaseLease(const CameraFrameLease_t *lease)
{
    uint32_t primask;
    SysResult_t result;

    // 私有旧入口身份不接受显式归还；零编号不代表任何真实持有
    if (lease == NULL || (uint32_t)lease->consumer >= CAMERA_FRAME_CONSUMER_COUNT ||
        lease->generation == 0U || lease->lease_id == 0U ||
        lease->buffer_id >= CAMERA_STREAM_BUFFER_COUNT)
        return SYS_ERR_INVALID_ARG;
    if (!CameraCapture_CanReturnStream())
        return SYS_ERR_INVALID_STATE;
    if (s_capture.config.format == CAMERA_PIXEL_FORMAT_JPEG)
        return SYS_ERR_NOT_SUPPORTED;

    primask = __get_PRIMASK();
    __disable_irq();
    result = CameraStream_Return(&s_capture.stream, lease);
    __set_PRIMASK(primask);
    if (result == SYS_OK)
        CameraCapture_FinishStreamDrain();
    return result;
}

//BSP完整帧回调，运行于中断上下文
static SysResult_t CameraStream_OnFrameComplete(
    BspDcmiTarget_t completed_target,
    void *context)
{
    CameraStream_t *stream = (CameraStream_t *)context;

    //只接受当前Media五缓冲实例
    if (stream != &s_capture.stream)
        return SYS_ERR_INVALID_ARG;
    if (s_capture.config.format == CAMERA_PIXEL_FORMAT_JPEG)
        return SYS_ERR_INVALID_STATE;

    //硬件地址替换成功后提交所有权，再通知任务处理进展
    {
        // 地址写后确认失败时寄存器可能已变，任务收到错误事件前也不可再发布帧
        if (s_capture.raw_rotation_failed)
            return SYS_ERR_INVALID_STATE;
        SysResult_t result = CameraStream_Rotate(stream, completed_target, BSP_DCMI_ReplaceTarget);
        if (result != SYS_OK)
            s_capture.raw_rotation_failed = true;
        if (s_capture_callback != NULL)
            s_capture_callback(result == SYS_OK ? CAMERA_CAPTURE_EVENT_PROGRESS :
                CAMERA_CAPTURE_EVENT_ERROR, s_capture_callback_context);
        return result;
    }
}

//任务中推进JPEG收尾、长度验证和超时，不在ISR扫描数据
static void CameraCapture_ProcessJpeg(void)
{
    uint32_t received = 0U;
    uint32_t valid = 0U;
    SysResult_t result;

    result = BSP_DCMI_ProcessJpeg(&received);
    if (result == SYS_OK)
    {
        s_capture.snapshot.received_bytes = received;
        if (received > s_capture.snapshot.frame_capacity || received == 0U)
            result = SYS_ERR_PROTOCOL;
        else
            result = CameraJpeg_Validate(s_capture.snapshot.framebuffer, received,
                &s_capture.config, &valid);
        if (result == SYS_OK)
        {
            s_capture.snapshot.valid_bytes = valid;
            s_capture.last_result = SYS_OK;
            s_capture.state = CAMERA_CAPTURE_READY;
            return;
        }
    }

    //未收尾时继续等；到期、截断或硬件错误都不能交付图像
    if (result == SYS_ERR_BUSY)
    {
        if ((uint32_t)(Platform_TimeGetMs() - s_capture.snapshot.start_tick) <
            s_capture.snapshot.timeout_ms)
            return;
        result = SYS_ERR_TIMEOUT;
    }
    s_capture.snapshot.valid_bytes = 0U;
    s_capture.last_result = result;
    s_capture.state = CAMERA_CAPTURE_ERROR;
    (void)CameraCapture_Stop();
}

//任务中校验JPEG段边界、分辨率和结束标记，成功才填写有效长度
static SysResult_t CameraJpeg_Validate(const uint8_t *data, uint32_t received,
    const CameraConfig_t *config, uint32_t *valid_bytes)
{
    uint32_t pos = 2U;
    uint32_t length;
    uint32_t marker;
    uint32_t components = 0U;
    bool sof = false;
    bool entropy = false;

    if (data == NULL || config == NULL || valid_bytes == NULL)
        return SYS_ERR_INVALID_ARG;
    if (received < 4U || data[0] != 0xFFU || data[1] != 0xD8U)
        return SYS_ERR_PROTOCOL;

    while (pos < received)
    {
        if (data[pos++] != 0xFFU)
            return SYS_ERR_PROTOCOL;
        while (pos < received && data[pos] == 0xFFU)
            pos++;
        if (pos >= received)
            return SYS_ERR_PROTOCOL;
        marker = data[pos++];
        if (marker == 0xD8U || marker == 0xD9U || marker == 0U ||
            (marker >= 0xD0U && marker <= 0xD7U) || received - pos < 2U)
            return SYS_ERR_PROTOCOL;
        length = ((uint32_t)data[pos] << 8) | data[pos + 1U];
        if (length < 2U || length > received - pos)
            return SYS_ERR_PROTOCOL;

        //组件输出Baseline JPEG；尺寸不符、重复SOF或渐进格式均不交付
        if (marker == 0xC0U)
        {
            if (sof || length < 8U || data[pos + 2U] != 8U)
                return SYS_ERR_PROTOCOL;
            components = data[pos + 7U];
            if ((components != 1U && components != 3U) || length != 8U + 3U * components ||
                (((uint32_t)data[pos + 3U] << 8) | data[pos + 4U]) != config->height ||
                (((uint32_t)data[pos + 5U] << 8) | data[pos + 6U]) != config->width)
                return SYS_ERR_PROTOCOL;
            sof = true;
        }
        else if (marker >= 0xC1U && marker <= 0xCFU &&
            marker != 0xC4U && marker != 0xC8U && marker != 0xCCU)
            return SYS_ERR_NOT_SUPPORTED;

        if (marker != 0xDAU)
        {
            pos += length;
            continue;
        }
        if (!sof || length < 6U || data[pos + 2U] != components ||
            length != 6U + 2U * components ||
            data[pos + length - 3U] != 0U ||
            data[pos + length - 2U] != 63U || data[pos + length - 1U] != 0U)
            return SYS_ERR_PROTOCOL;
        pos += length;

        //熵数据中的FF00和重启标记不是EOI，APP段内的伪EOI已由段长跳过
        while (pos < received)
        {
            if (data[pos++] != 0xFFU)
            {
                entropy = true;
                continue;
            }
            while (pos < received && data[pos] == 0xFFU)
                pos++;
            if (pos >= received)
                return SYS_ERR_PROTOCOL;
            marker = data[pos++];
            if (marker == 0U || (marker >= 0xD0U && marker <= 0xD7U))
                continue;
            if (marker != 0xD9U || !entropy)
                return SYS_ERR_PROTOCOL;

            //按JPEG结构确认首个EOI，只交付完整图像，不把接收尾部当作文件内容
            *valid_bytes = pos;
            return SYS_OK;
        }
        return SYS_ERR_PROTOCOL;
    }
    return SYS_ERR_PROTOCOL;
}

//启动独立FIFO并登记Stream生命周期
static SysResult_t CameraJpegStream_Start(void)
{
    SysResult_t result;

    //先占用Stream模式，启动失败仍交给统一Stop清理硬件
    s_capture.mode = CAMERA_CAPTURE_MODE_STREAM;
    s_capture.state = CAMERA_CAPTURE_CAPTURING;
    result = CameraMjpegFifo_Start(&s_capture.config);
    s_capture.last_result = result;
    if (result != SYS_OK)
        s_capture.state = CAMERA_CAPTURE_ERROR;
    return result;
}
