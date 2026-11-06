#include "camera_mjpeg_fifo.h"
#include "bsp_dcmi.h"
#include "bsp_sdram.h"
#include "stm32h7xx.h"
#include <string.h>

//任务每次最多解析一个块，避免长时间占用Camera任务
#define MJPEG_PROCESS_BUDGET CAMERA_MJPEG_BLOCK_BYTES
#define MJPEG_MAX_FRAME_BYTES (65535UL * 4UL)
#define MJPEG_INVALID_BLOCK UINT8_MAX

#if CAMERA_MJPEG_REGION_END > BSP_SDRAM_END || \
    CAMERA_MJPEG_FIFO_BASE < BSP_SDRAM_BASE || \
    CAMERA_MJPEG_BLOCK_BYTES * CAMERA_MJPEG_BLOCK_COUNT != CAMERA_MJPEG_FIFO_BYTES || \
    (CAMERA_MJPEG_FIFO_BASE % 32U) != 0U || (CAMERA_MJPEG_BLOCK_BYTES % 16U) != 0U
#error JPEG FIFO must fit the non-cacheable SDRAM window
#endif

//FREE允许DMA申请；SEALED只能由任务解析和引用管理回收
typedef enum
{
    MJPEG_BLOCK_FREE = 0,
    MJPEG_BLOCK_DMA,
    MJPEG_BLOCK_SEALED
} MjpegBlockState_t;

//每块只保存元数据，不另设图像接收数组
typedef struct
{
    MjpegBlockState_t state;

    //同一块可包含多张JPEG，每张READY或IN_USE各持一份引用
    uint16_t references;

    //全部字节解析后才允许按引用计数回收
    bool parsed;

    //丢字节后换代，禁止拼接不连续的数据
    uint32_t epoch;
} MjpegBlock_t;

//帧描述只保存范围和块引用，不复制JPEG
typedef struct
{
    uint32_t offset;
    uint32_t length;
    uint64_t blocks;
    uint32_t epoch;
} MjpegFrame_t;

//解析器跳过段内容，避免把APP数据中的FFD9当成帧尾
typedef enum
{
    MJPEG_SEEK_FF = 0,
    MJPEG_SEEK_SOI,
    MJPEG_MARKER_FF,
    MJPEG_MARKER_CODE,
    MJPEG_LENGTH_HIGH,
    MJPEG_LENGTH_LOW,
    MJPEG_SEGMENT,
    MJPEG_ENTROPY,
    MJPEG_ENTROPY_FF,
    MJPEG_WAIT_FRAME
} MjpegParseState_t;

//只在Camera任务解析JPEG，ISR只更新块和输入边界
static struct
{
    //保存本轮图像尺寸和格式，不反向读取Application配置
    CameraConfig_t config;

    //底层失败时仍保持占用，直到Stop确认成功
    volatile bool running;

    //锁存元数据错误，不能通过重置计数掩盖错误
    SysResult_t result;

    //ISR与任务共享的块状态，通过短临界区访问
    volatile MjpegBlock_t blocks[CAMERA_MJPEG_BLOCK_COUNT];

    //M0/M1关联的块索引，无效值表示正在写独立丢弃区
    volatile uint8_t targets[2];

    //只按物理顺序申请下一块，保持可用两段表达跨尾JPEG
    volatile uint8_t next_block;

    //空间不足时继续接收并丢弃，不停止DCMI
    volatile bool dropping;

    //每次字节流中断递增，任务不能跨代拼接图像
    volatile uint32_t epoch;

    //IRQ到任务的完成块队列，元素不是图像数据
    volatile uint8_t completed[CAMERA_MJPEG_BLOCK_COUNT];
    volatile uint8_t completed_head;
    volatile uint8_t completed_count;

    //输入FRAME计数只作为边界证据，不能代替DMA完成
    volatile uint32_t input_frames;

    //记录写入丢弃区的块数，不把块数当作准确丢帧数
    uint32_t dropped_blocks;

    //任务正在读取的SEALED块，不解析活动DMA目标
    uint8_t parse_block;
    uint32_t parse_offset;
    uint32_t parse_epoch;
    uint32_t task_epoch;

    //READY队列与唯一IN_USE描述分别持有块引用
    MjpegFrame_t ready[CAMERA_MJPEG_FRAME_QUEUE];
    uint8_t ready_head;
    uint8_t ready_count;
    MjpegFrame_t loan;
    bool loaned;
    uint32_t completed_frames;
    uint32_t dropped_frames;

    //保存跨块解析位置和小段头，不存整张JPEG
    MjpegParseState_t parser;
    MjpegFrame_t partial;
    uint8_t marker;
    uint16_t segment_length;
    uint16_t segment_left;
    uint8_t header[18];
    uint8_t header_size;
    uint8_t components;
    bool saw_sof;
    bool saw_entropy;
    uint32_t frame_ticket;
} s_fifo;

//登记完成块并申请下一个DMA块
static SysResult_t Mjpeg_BlockComplete(BspDcmiTarget_t target,
    uint32_t completed_address, uint32_t *next_address, void *context);

//只登记输入帧结束，不能在这里交付JPEG
static void Mjpeg_FrameBoundary(void *context);

//取得FIFO块的物理起始地址
static uint32_t Mjpeg_BlockAddress(uint8_t index);

//在短临界区释放一张帧描述的全部块引用
static void Mjpeg_ReleaseBlocks(uint64_t blocks);

//放弃未完成图像并回到SOI搜索
static void Mjpeg_ResetParser(void);

//丢弃尚未借出的READY，保留USB借用的描述
static void Mjpeg_DropReady(void);

//让当前图像持有一个新块，防止解析后被DMA复用
static void Mjpeg_HoldBlock(uint8_t index);

//解析一个已确认写入SDRAM的字节
static void Mjpeg_ParseByte(uint8_t value, uint32_t offset, uint8_t block);

//校验SOF或SOS段头并进入下一解析状态
static void Mjpeg_EndSegment(void);

//同时具备完整JPEG与输入帧边界后发布描述
static bool Mjpeg_Publish(void);

//静态回调表只传递元数据，BSP不依赖Media类型
static const BspDcmiByteStreamOps_t s_byte_ops =
{
    Mjpeg_BlockComplete,
    Mjpeg_FrameBoundary
};

//启动独立JPEG字节FIFO
SysResult_t CameraMjpegFifo_Start(const CameraConfig_t *config)
{
    SysResult_t result;

    //只开放已接入的JPEG尺寸，接收容量不能代替格式校验
    if (config == NULL || config->width == 0U || config->height == 0U)
        return SYS_ERR_INVALID_ARG;
    if (config->format != CAMERA_PIXEL_FORMAT_JPEG ||
        !CameraConfig_IsJpegSizeSupported(config))
        return SYS_ERR_NOT_SUPPORTED;
    if (s_fifo.running || s_fifo.loaned)
        return SYS_ERR_BUSY;

    //上一轮必须安全停止，才能重建描述与引用
    memset(&s_fifo, 0, sizeof(s_fifo));
    s_fifo.config = *config;
    s_fifo.epoch = 1U;
    s_fifo.task_epoch = 1U;
    s_fifo.parse_block = MJPEG_INVALID_BLOCK;
    s_fifo.targets[0] = 0U;
    s_fifo.targets[1] = 1U;
    s_fifo.next_block = 2U;
    s_fifo.blocks[0].state = MJPEG_BLOCK_DMA;
    s_fifo.blocks[1].state = MJPEG_BLOCK_DMA;
    s_fifo.blocks[0].epoch = s_fifo.epoch;
    s_fifo.blocks[1].epoch = s_fifo.epoch;
    s_fifo.running = true;

    //回调上下文先就绪，再开放DMA和连续CAPTURE
    result = BSP_DCMI_StartJpegContinuous(
        (uint32_t *)(uintptr_t)Mjpeg_BlockAddress(0U),
        (uint32_t *)(uintptr_t)Mjpeg_BlockAddress(1U),
        CAMERA_MJPEG_BLOCK_BYTES / sizeof(uint32_t), &s_byte_ops, &s_fifo);
    s_fifo.result = result;

    //失败时仍保留占用，必须由上层成功Stop后清理
    return result;
}

//解析已完成块并维护READY描述
SysResult_t CameraMjpegFifo_Process(void)
{
    uint32_t mask;
    uint32_t epoch;
    uint32_t budget = MJPEG_PROCESS_BUDGET;
    uint8_t block;
    bool skip;

    if (!s_fifo.running)
        return SYS_ERR_INVALID_STATE;
    if (s_fifo.result != SYS_OK)
        return s_fifo.result;

    while (budget != 0U)
    {
        //ISR换代只表示发生过字节缺口，旧IN_USE仍由原持有者归还
        mask = __get_PRIMASK();
        __disable_irq();
        epoch = s_fifo.epoch;
        __set_PRIMASK(mask);
        if (s_fifo.task_epoch != epoch)
        {
            Mjpeg_ResetParser();
            Mjpeg_DropReady();
            s_fifo.task_epoch = epoch;
            s_fifo.frame_ticket = s_fifo.input_frames;
        }

        //EOI可能先于FRAME IRQ被任务看到，等待时不扫描下一帧
        if (s_fifo.parser == MJPEG_WAIT_FRAME && !Mjpeg_Publish())
            return s_fifo.result;

        //从ISR队列取得一个已退出DMA的块
        if (s_fifo.parse_block == MJPEG_INVALID_BLOCK)
        {
            mask = __get_PRIMASK();
            __disable_irq();
            if (s_fifo.completed_count == 0U)
            {
                __set_PRIMASK(mask);
                return SYS_OK;
            }
            block = s_fifo.completed[s_fifo.completed_head];
            s_fifo.completed_head = (uint8_t)((s_fifo.completed_head + 1U) % CAMERA_MJPEG_BLOCK_COUNT);
            s_fifo.completed_count--;
            s_fifo.parse_epoch = s_fifo.blocks[block].epoch;
            s_fifo.parse_block = block;
            s_fifo.parse_offset = 0U;
            __DMB();
            __set_PRIMASK(mask);
        }
        block = s_fifo.parse_block;
        skip = s_fifo.parse_epoch != s_fifo.task_epoch;

        //仅在任务中逐字节解析；不读取NDTR推测尚未落入内存的数据
        if (!skip)
        {
            const uint8_t *data = (const uint8_t *)(uintptr_t)Mjpeg_BlockAddress(block);
            uint32_t offset = (uint32_t)block * CAMERA_MJPEG_BLOCK_BYTES + s_fifo.parse_offset;
            Mjpeg_ParseByte(data[s_fifo.parse_offset], offset, block);
            s_fifo.parse_offset++;
            budget--;
        }
        else
            s_fifo.parse_offset = CAMERA_MJPEG_BLOCK_BYTES;

        //解析引用保留，只有已解析且无人引用的块才重新变为FREE
        if (s_fifo.parse_offset == CAMERA_MJPEG_BLOCK_BYTES)
        {
            mask = __get_PRIMASK();
            __disable_irq();
            s_fifo.blocks[block].parsed = true;
            if (s_fifo.blocks[block].references == 0U)
                s_fifo.blocks[block].state = MJPEG_BLOCK_FREE;
            s_fifo.parse_block = MJPEG_INVALID_BLOCK;
            __set_PRIMASK(mask);
        }
        if (s_fifo.result != SYS_OK)
            return s_fifo.result;
    }

    //每个TC及FRAME都会唤醒任务；剩余块由下一次任务运行继续解析
    return SYS_OK;
}

//借出一张完整JPEG，不复制FIFO内容
bool CameraMjpegFifo_GetFrame(CameraFrame_t *frame)
{
    uint32_t first;

    if (frame == NULL || !s_fifo.running || s_fifo.result != SYS_OK ||
        s_fifo.loaned || s_fifo.ready_count == 0U)
        return false;

    //描述移出READY队列，但块引用保持到ReleaseFrame
    s_fifo.loan = s_fifo.ready[s_fifo.ready_head];
    s_fifo.ready_head = (uint8_t)((s_fifo.ready_head + 1U) % CAMERA_MJPEG_FRAME_QUEUE);
    s_fifo.ready_count--;
    s_fifo.loaned = true;
    first = CAMERA_MJPEG_FIFO_BYTES - s_fifo.loan.offset;
    if (first > s_fifo.loan.length)
        first = s_fifo.loan.length;
    frame->width = s_fifo.config.width;
    frame->height = s_fifo.config.height;
    frame->format = CAMERA_PIXEL_FORMAT_JPEG;
    frame->data = (const uint8_t *)(uintptr_t)(CAMERA_MJPEG_FIFO_BASE + s_fifo.loan.offset);
    frame->size_bytes = s_fifo.loan.length;
    frame->size_bytes2 = s_fifo.loan.length - first;
    frame->data2 = frame->size_bytes2 == 0U ? NULL :
        (const uint8_t *)(uintptr_t)CAMERA_MJPEG_FIFO_BASE;
    return true;
}

//归还唯一借出JPEG的块引用
SysResult_t CameraMjpegFifo_ReleaseFrame(void)
{
    uint32_t i;
    uint32_t mask;

    if (!s_fifo.loaned)
        return SYS_ERR_INVALID_STATE;
    mask = __get_PRIMASK();
    __disable_irq();

    //先核对全部引用，再一次性归还，失败重试不能重复减引用
    for (i = 0U; i < CAMERA_MJPEG_BLOCK_COUNT; i++)
    {
        if ((s_fifo.loan.blocks & (UINT64_C(1) << i)) != 0U &&
            s_fifo.blocks[i].references == 0U)
        {
            __set_PRIMASK(mask);
            return SYS_ERR_INVALID_STATE;
        }
    }
    Mjpeg_ReleaseBlocks(s_fifo.loan.blocks);
    memset(&s_fifo.loan, 0, sizeof(s_fifo.loan));
    s_fifo.loaned = false;
    __set_PRIMASK(mask);
    return SYS_OK;
}

//查询是否存在尚未归还的帧
bool CameraMjpegFifo_HasLoan(void)
{
    return s_fifo.loaned;
}

//底层确认停止后释放接收端，借出帧继续保留
void CameraMjpegFifo_OnStopped(void)
{
    uint32_t i;
    uint32_t mask;

    //调用者必须先确认DMA EN与DCMI CAPTURE停止
    s_fifo.running = false;
    Mjpeg_ResetParser();
    Mjpeg_DropReady();
    mask = __get_PRIMASK();
    __disable_irq();
    for (i = 0U; i < CAMERA_MJPEG_BLOCK_COUNT; i++)
    {
        s_fifo.blocks[i].parsed = true;
        s_fifo.blocks[i].state = s_fifo.blocks[i].references == 0U ?
            MJPEG_BLOCK_FREE : MJPEG_BLOCK_SEALED;
    }
    s_fifo.completed_count = 0U;
    s_fifo.parse_block = MJPEG_INVALID_BLOCK;
    __set_PRIMASK(mask);
}

//取得有效JPEG计数和已丢弃READY计数
void CameraMjpegFifo_GetStats(uint32_t *completed, uint32_t *dropped)
{
    if (completed != NULL)
        *completed = s_fifo.completed_frames;
    if (dropped != NULL)
        *dropped = s_fifo.dropped_frames;
}

//取得块物理地址
static uint32_t Mjpeg_BlockAddress(uint8_t index)
{
    return CAMERA_MJPEG_FIFO_BASE + (uint32_t)index * CAMERA_MJPEG_BLOCK_BYTES;
}

//登记完成块并申请下一块DMA地址
static SysResult_t Mjpeg_BlockComplete(BspDcmiTarget_t target,
    uint32_t completed_address, uint32_t *next_address, void *context)
{
    uint8_t index;
    uint8_t next;
    uint32_t expected;
    volatile MjpegBlock_t *entry;

    if (context != &s_fifo || !s_fifo.running || next_address == NULL ||
        (target != BSP_DCMI_TARGET_M0 && target != BSP_DCMI_TARGET_M1))
        return SYS_ERR_INVALID_STATE;
    index = s_fifo.targets[target];
    expected = index == MJPEG_INVALID_BLOCK ?
        CAMERA_MJPEG_DROP_BASE + (uint32_t)target * CAMERA_MJPEG_BLOCK_BYTES : Mjpeg_BlockAddress(index);
    if (completed_address != expected)
        return SYS_ERR_PROTOCOL;

    //完成块进入SEALED队列，任务确认解析完之前不再分配给DMA
    if (index != MJPEG_INVALID_BLOCK)
    {
        entry = &s_fifo.blocks[index];
        if (entry->state != MJPEG_BLOCK_DMA || entry->references != 0U ||
            s_fifo.completed_count >= CAMERA_MJPEG_BLOCK_COUNT)
            return SYS_ERR_PROTOCOL;
        //TC确认Word写入完成，再发布可读取的块元数据
        __DMB();
        entry->state = MJPEG_BLOCK_SEALED;
        entry->parsed = false;
        s_fifo.completed[(s_fifo.completed_head + s_fifo.completed_count) %
            CAMERA_MJPEG_BLOCK_COUNT] = index;
        s_fifo.completed_count++;
    }
    else
        s_fifo.dropped_blocks++;

    //只申请顺序上的下一个FREE块，不在ISR扫描队列或覆盖借出帧
    next = s_fifo.next_block;
    entry = &s_fifo.blocks[next];
    if (entry->state == MJPEG_BLOCK_FREE && entry->references == 0U)
    {
        entry->state = MJPEG_BLOCK_DMA;
        entry->parsed = false;
        entry->epoch = s_fifo.epoch;
        s_fifo.targets[target] = next;
        s_fifo.next_block = (uint8_t)((next + 1U) % CAMERA_MJPEG_BLOCK_COUNT);
        s_fifo.dropping = false;
        *next_address = Mjpeg_BlockAddress(next);
    }
    else
    {
        //无安全空间时写独立丢弃区，CAPTURE继续运行，旧引用仍保留
        if (!s_fifo.dropping)
        {
            if (s_fifo.epoch == UINT32_MAX)
                return SYS_ERR_NO_RESOURCE;
            s_fifo.epoch++;
            s_fifo.dropping = true;
        }
        s_fifo.targets[target] = MJPEG_INVALID_BLOCK;
        *next_address = CAMERA_MJPEG_DROP_BASE + (uint32_t)target * CAMERA_MJPEG_BLOCK_BYTES;
    }
    return SYS_OK;
}

//登记输入边界，IRQ不检查SOI和EOI
static void Mjpeg_FrameBoundary(void *context)
{
    if (context == &s_fifo && s_fifo.running)
        s_fifo.input_frames++;
}

//释放描述引用，不释放仍由另一张图像持有的同一个块
static void Mjpeg_ReleaseBlocks(uint64_t blocks)
{
    uint32_t mask = __get_PRIMASK();
    uint32_t i;

    __disable_irq();
    for (i = 0U; i < CAMERA_MJPEG_BLOCK_COUNT; i++)
    {
        volatile MjpegBlock_t *entry = &s_fifo.blocks[i];
        if ((blocks & (UINT64_C(1) << i)) == 0U)
            continue;
        if (entry->references == 0U)
        {
            s_fifo.result = SYS_ERR_PROTOCOL;
            continue;
        }
        entry->references--;
        if (entry->references == 0U && entry->parsed)
            entry->state = MJPEG_BLOCK_FREE;
    }
    __set_PRIMASK(mask);
}

//丢弃当前未完成JPEG并回到SOI搜索
static void Mjpeg_ResetParser(void)
{
    Mjpeg_ReleaseBlocks(s_fifo.partial.blocks);
    memset(&s_fifo.partial, 0, sizeof(s_fifo.partial));
    s_fifo.parser = MJPEG_SEEK_FF;
    s_fifo.saw_sof = false;
    s_fifo.saw_entropy = false;
    s_fifo.components = 0U;
}

//只丢弃未借出的READY，不碰loan描述
static void Mjpeg_DropReady(void)
{
    while (s_fifo.ready_count != 0U)
    {
        Mjpeg_ReleaseBlocks(s_fifo.ready[s_fifo.ready_head].blocks);
        s_fifo.ready_head = (uint8_t)((s_fifo.ready_head + 1U) % CAMERA_MJPEG_FRAME_QUEUE);
        s_fifo.ready_count--;
        s_fifo.dropped_frames++;
    }
}

//为正在解析的图像持有一个块
static void Mjpeg_HoldBlock(uint8_t index)
{
    uint64_t bit = UINT64_C(1) << index;
    uint32_t mask;

    if ((s_fifo.partial.blocks & bit) != 0U)
        return;
    mask = __get_PRIMASK();
    __disable_irq();
    //引用上限只来自8张READY、1张借出和1张正在解析的图像
    if (s_fifo.blocks[index].state != MJPEG_BLOCK_SEALED ||
        s_fifo.blocks[index].references >= CAMERA_MJPEG_FRAME_QUEUE + 2U)
    {
        s_fifo.result = SYS_ERR_PROTOCOL;
        __set_PRIMASK(mask);
        return;
    }
    s_fifo.blocks[index].references++;
    s_fifo.partial.blocks |= bit;
    __set_PRIMASK(mask);
}

//解析单个字节，不跨过段长或熵数据边界
static void Mjpeg_ParseByte(uint8_t value, uint32_t offset, uint8_t block)
{
    if (s_fifo.parser == MJPEG_SEEK_FF)
    {
        if (value != 0xFFU)
            return;
        s_fifo.partial.offset = offset;
        s_fifo.partial.length = 1U;
        s_fifo.partial.epoch = s_fifo.parse_epoch;
        Mjpeg_HoldBlock(block);
        s_fifo.parser = MJPEG_SEEK_SOI;
        return;
    }

    //保存块引用后再推进字节范围，跨FIFO末尾仍是逻辑连续图像
    Mjpeg_HoldBlock(block);
    s_fifo.partial.length++;
    if (s_fifo.partial.length > MJPEG_MAX_FRAME_BYTES)
    {
        s_fifo.dropped_frames++;
        Mjpeg_ResetParser();
        return;
    }
    switch (s_fifo.parser)
    {
        case MJPEG_SEEK_SOI:
            if (value == 0xD8U)
                s_fifo.parser = MJPEG_MARKER_FF;
            else
            {
                Mjpeg_ResetParser();
                if (value == 0xFFU)
                    Mjpeg_ParseByte(value, offset, block);
            }
            break;
        case MJPEG_MARKER_FF:
            if (value == 0xFFU)
                s_fifo.parser = MJPEG_MARKER_CODE;
            else
                Mjpeg_ResetParser();
            break;
        case MJPEG_MARKER_CODE:
            if (value == 0xFFU)
                break;
            if (value == 0U || value == 0xD8U || value == 0xD9U ||
                (value >= 0xD0U && value <= 0xD7U))
            {
                Mjpeg_ResetParser();
                break;
            }
            s_fifo.marker = value;
            s_fifo.parser = MJPEG_LENGTH_HIGH;
            break;
        case MJPEG_LENGTH_HIGH:
            s_fifo.segment_length = (uint16_t)value << 8;
            s_fifo.parser = MJPEG_LENGTH_LOW;
            break;
        case MJPEG_LENGTH_LOW:
            s_fifo.segment_length |= value;
            if (s_fifo.segment_length < 2U)
            {
                Mjpeg_ResetParser();
                break;
            }
            s_fifo.segment_left = s_fifo.segment_length - 2U;
            s_fifo.header_size = 0U;
            s_fifo.parser = MJPEG_SEGMENT;
            if (s_fifo.segment_left == 0U)
                Mjpeg_EndSegment();
            break;
        case MJPEG_SEGMENT:
            if (s_fifo.header_size < sizeof(s_fifo.header))
                s_fifo.header[s_fifo.header_size++] = value;
            s_fifo.segment_left--;
            if (s_fifo.segment_left == 0U)
                Mjpeg_EndSegment();
            break;
        case MJPEG_ENTROPY:
            if (value == 0xFFU)
                s_fifo.parser = MJPEG_ENTROPY_FF;
            else
                s_fifo.saw_entropy = true;
            break;
        case MJPEG_ENTROPY_FF:
            if (value == 0xFFU)
                break;
            if (value == 0U || (value >= 0xD0U && value <= 0xD7U))
            {
                //FF00代表一个数据字节，RST标记本身不算熵数据
                if (value == 0U)
                    s_fifo.saw_entropy = true;
                s_fifo.parser = MJPEG_ENTROPY;
            }
            else if (value == 0xD9U && s_fifo.saw_sof && s_fifo.saw_entropy)
                s_fifo.parser = MJPEG_WAIT_FRAME;
            else
                Mjpeg_ResetParser();
            break;
        default:
            s_fifo.result = SYS_ERR_PROTOCOL;
            break;
    }
}

//校验尺寸与Baseline扫描头，其他段只按声明长度跳过
static void Mjpeg_EndSegment(void)
{
    const uint8_t *h = s_fifo.header;
    uint8_t marker = s_fifo.marker;
    bool valid = true;

    if (marker == 0xC0U)
    {
        valid = !s_fifo.saw_sof && s_fifo.segment_length >= 8U && h[0] == 8U;
        if (valid)
        {
            s_fifo.components = h[5];
            valid = (s_fifo.components == 1U || s_fifo.components == 3U) &&
                s_fifo.segment_length == 8U + 3U * s_fifo.components &&
                (((uint32_t)h[1] << 8) | h[2]) == s_fifo.config.height &&
                (((uint32_t)h[3] << 8) | h[4]) == s_fifo.config.width;
        }
        s_fifo.saw_sof = valid;
    }
    else if (marker >= 0xC1U && marker <= 0xCFU &&
        marker != 0xC4U && marker != 0xC8U && marker != 0xCCU)
        valid = false;
    else if (marker == 0xDAU)
    {
        valid = s_fifo.saw_sof && s_fifo.segment_length == 6U + 2U * s_fifo.components &&
            h[0] == s_fifo.components;
        if (valid)
        {
            uint32_t tail = s_fifo.segment_length - 5U;
            valid = h[tail] == 0U && h[tail + 1U] == 63U && h[tail + 2U] == 0U;
        }
    }
    if (!valid)
    {
        s_fifo.dropped_frames++;
        Mjpeg_ResetParser();
    }
    else
        s_fifo.parser = marker == 0xDAU ? MJPEG_ENTROPY : MJPEG_MARKER_FF;
}

//合并输入帧证据与已落入内存的完整JPEG，提交READY描述
static bool Mjpeg_Publish(void)
{
    uint32_t mask;
    uint8_t tail;
    uint32_t next_ticket = s_fifo.frame_ticket + 1U;

    //只用已完成块解析出的EOI；FRAME本身不能证明内存收尾
    if ((int32_t)(s_fifo.input_frames - next_ticket) < 0)
        return false;
    mask = __get_PRIMASK();
    __disable_irq();
    if (s_fifo.partial.epoch != s_fifo.epoch)
    {
        __set_PRIMASK(mask);
        return false;
    }
    __set_PRIMASK(mask);

    //队列满时只回收最老READY，IN_USE另存且不能被覆盖
    if (s_fifo.ready_count == CAMERA_MJPEG_FRAME_QUEUE)
    {
        Mjpeg_ReleaseBlocks(s_fifo.ready[s_fifo.ready_head].blocks);
        s_fifo.ready_head = (uint8_t)((s_fifo.ready_head + 1U) % CAMERA_MJPEG_FRAME_QUEUE);
        s_fifo.ready_count--;
        s_fifo.dropped_frames++;
    }
    tail = (uint8_t)((s_fifo.ready_head + s_fifo.ready_count) % CAMERA_MJPEG_FRAME_QUEUE);
    s_fifo.ready[tail] = s_fifo.partial;
    s_fifo.ready_count++;
    s_fifo.completed_frames++;
    s_fifo.frame_ticket = next_ticket;

    //引用从解析器转移到READY，不重复加引用，也不复制图像
    memset(&s_fifo.partial, 0, sizeof(s_fifo.partial));
    s_fifo.parser = MJPEG_SEEK_FF;
    s_fifo.saw_sof = false;
    s_fifo.saw_entropy = false;
    s_fifo.components = 0U;
    return true;
}
