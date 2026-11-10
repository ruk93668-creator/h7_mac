//FatFS磁盘接口适配，不解析文件名，也不改变SDMMC传输算法
#include <stddef.h>
#include <string.h>
#include "ff.h"
#include "diskio.h"
#include "fatfs_diskio.h"
#include "sd_block.h"
#include "platform_time.h"

//只暴露一个逻辑卷，分区编号由只读预检查确定
PARTITION VolToPart[FF_VOLUMES] = {{0U, 0U}};

//保存已检查卷的起始LBA
static uint32_t s_first;

//保存已检查卷的扇区数量
static uint32_t s_count;

//记录当前卷是否经过配置
static bool s_configured;

//默认关闭写入，挂载验证完成后才能显式打开
static bool s_writable;

//记录当前磁盘会话是否已遇到不可继续的I/O故障
static bool s_fault;

//保存一次文件操作中的首个底层错误
static SysResult_t s_last_result;

//仅记录通过检查后真正下发的请求，拒绝访问不污染所有者统计
volatile FatFsDiskWriteDebug_t g_sd_write_debug;

//只由卷所有者更新，不在中断中采集耗时
volatile FatFsIoTimingDebug_t g_storage_io_timing;

//转换块设备错误并保留原始原因
static DRESULT Disk_Result(SysResult_t result);

//提交一次请求分布，不增加等待、磁盘访问或中断屏蔽
static bool Disk_RecordWrite(uint32_t sector, uint32_t count);

//记录原有块同步检查耗时，不增加检查次数
static SysResult_t Disk_Sync(void);

//检查磁盘号、扇区边界和任务所有权
static SysResult_t Disk_Check(BYTE drive, LBA_t sector, UINT count);

//配置单卷映射及边界
SysResult_t FatFsDisk_Configure(uint8_t partition, uint32_t first, uint32_t count)
{
    BspSdmmcCardInfo_t info;
    SysResult_t result;

    //先确认所有权，避免其他任务更改正在使用的卷
    if (!SD_Block_IsOwner(SD_BLOCK_CLIENT_FATFS))
        return SYS_ERR_INVALID_STATE;
    if (partition > 4U || count == 0U)
        return SYS_ERR_INVALID_ARG;
    result = SD_Block_GetInfo(&info);
    if (result != SYS_OK)
        return result;
    if (first >= info.logical_block_count || count > info.logical_block_count - first)
        return SYS_ERR_INVALID_ARG;

    //新卷配置开始新的诊断会话，挂载失败时布局仍保持无效
    {
        uint32_t session = g_sd_write_debug.session;
        memset((void *)&g_sd_write_debug, 0, sizeof(g_sd_write_debug));
        memset((void *)&g_storage_io_timing, 0, sizeof(g_storage_io_timing));
        g_sd_write_debug.session = session == UINT32_MAX ? session : session + 1U;
    }

    //配置期间只读，不能因调用Configure就开始写卡
    s_first = first;
    s_count = count;
    s_configured = true;
    s_writable = false;
    s_fault = false;
    s_last_result = SYS_OK;
    VolToPart[0].pt = partition;
    return SYS_OK;
}

//设置已挂载卷的写权限
SysResult_t FatFsDisk_SetWritable(bool writable)
{
    if (!SD_Block_IsOwner(SD_BLOCK_CLIENT_FATFS) || !s_configured || s_fault)
        return SYS_ERR_INVALID_STATE;
    s_writable = writable;
    return SYS_OK;
}

//撤销本地卷配置
void FatFsDisk_Reset(void)
{
    //非所有者不能取消另一任务的写保护和故障状态
    if (!SD_Block_IsOwner(SD_BLOCK_CLIENT_FATFS))
        return;
    s_writable = false;
    s_configured = false;
    s_fault = false;
    s_first = 0U;
    s_count = 0U;
    VolToPart[0].pt = 0U;
}

//清除本次调用的首错记录
void FatFsDisk_Begin(void)
{
    if (SD_Block_IsOwner(SD_BLOCK_CLIENT_FATFS))
        s_last_result = SYS_OK;
}

//读取本次调用的底层首错
SysResult_t FatFsDisk_GetLastResult(void)
{
    return s_last_result;
}

//初始化物理磁盘，不格式化卷
DSTATUS disk_initialize(BYTE drive)
{
    SysResult_t result;

    if (drive != 0U || !SD_Block_IsOwner(SD_BLOCK_CLIENT_FATFS) || !s_configured)
        return STA_NOINIT;
    if (s_fault)
        return STA_NOINIT;
    result = SD_Block_Init();
    if (result != SYS_OK)
    {
        (void)Disk_Result(result);
        return STA_NOINIT;
    }
    return disk_status(drive);
}

//查询当前插卡和初始化状态
DSTATUS disk_status(BYTE drive)
{
    SdBlockStatus_t status;
    SysResult_t result;

    if (drive != 0U || !s_configured || !SD_Block_IsOwner(SD_BLOCK_CLIENT_FATFS))
        return STA_NOINIT;
    result = SD_Block_GetStatus(&status);
    if (result != SYS_OK)
    {
        (void)Disk_Result(result);
        return STA_NOINIT;
    }
    if (!status.present)
    {
        (void)Disk_Result(SYS_ERR_NOT_FOUND);
        return STA_NODISK | STA_NOINIT;
    }
    if (!status.initialized || status.fault || s_fault)
        return STA_NOINIT;
    return s_writable ? 0U : STA_PROTECT;
}

//读取绝对扇区，分区偏移只由FatFS计算一次
DRESULT disk_read(BYTE drive, BYTE *buffer, LBA_t sector, UINT count)
{
    SysResult_t result;
    uint32_t started;

    if (buffer == NULL)
        return RES_PARERR;
    result = Disk_Check(drive, sector, count);
    if (result != SYS_OK)
        return Disk_Result(result);
    //只包围原有块读调用，保留原超时和返回结果
    started = Platform_TimeGetMs();
    result = SD_Block_Read((uint32_t)sector, count, buffer, 2000U);
    FatFsDisk_RecordTiming(&g_storage_io_timing.block_read, Platform_TimeGetMs() - started);
    return Disk_Result(result);
}

//写入已检查卷，禁止越界改写其他分区
DRESULT disk_write(BYTE drive, const BYTE *buffer, LBA_t sector, UINT count)
{
    SysResult_t result;
    bool sampled;
    uint32_t started;

    if (buffer == NULL)
        return RES_PARERR;
    result = Disk_Check(drive, sector, count);
    if (result != SYS_OK)
        return Disk_Result(result);
    if (!s_writable)
        return RES_WRPRT;

    //减法检查避免sector加count回绕，范围外一律不下发底层
    if (sector < s_first || //起点不能在卷之前
        sector - s_first >= s_count || //起点必须在卷内
        count > s_count - (sector - s_first)) //数量不能越过卷末尾
        return RES_PARERR;

    //只有真正下发的请求才入账，统计不改变LBA、扇区数或超时
    sampled = Disk_RecordWrite((uint32_t)sector, count);
    started = Platform_TimeGetMs();
    result = SD_Block_Write((uint32_t)sector, count, buffer, 3000U);
    FatFsDisk_RecordTiming(&g_storage_io_timing.block_write, Platform_TimeGetMs() - started);
    if (sampled)
    {

        //错误请求可能部分写入，不能把整个请求计作成功扇区
        if (result == SYS_OK)
        {
            g_sd_write_debug.success_calls++;
            g_sd_write_debug.successful_sectors += count;
        }
        else
            g_sd_write_debug.failed_calls++;
        g_sd_write_debug.in_flight = 0U;
    }
    return Disk_Result(result);
}

//提供同步及介质几何参数，不实现擦除和格式化命令
DRESULT disk_ioctl(BYTE drive, BYTE command, void *buffer)
{
    BspSdmmcCardInfo_t info;
    SysResult_t result;

    if (drive != 0U || !s_configured || s_fault || !SD_Block_IsOwner(SD_BLOCK_CLIENT_FATFS))
        return RES_NOTRDY;

    //底层写入已等到卡TRANSFER，仍要复核掉卡和故障
    if (command == CTRL_SYNC)
        return Disk_Result(Disk_Sync());
    if (buffer == NULL)
        return RES_PARERR;
    result = SD_Block_GetInfo(&info);
    if (result != SYS_OK)
        return Disk_Result(result);
    switch (command)
    {
        case GET_SECTOR_COUNT:
            *(LBA_t *)buffer = info.logical_block_count;
            return RES_OK;
        case GET_SECTOR_SIZE:
            *(WORD *)buffer = (WORD)BSP_SDMMC_BLOCK_SIZE;
            return RES_OK;
        case GET_BLOCK_SIZE:
            *(DWORD *)buffer = 1U;
            return RES_OK;
        default:
            return RES_PARERR;
    }
}

//转换一次块操作结果
static DRESULT Disk_Result(SysResult_t result)
{
    //拒绝其他任务或ISR时，不污染所有者正在记录的结果
    if (!SD_Block_IsOwner(SD_BLOCK_CLIENT_FATFS))
        return RES_NOTRDY;
    if (result == SYS_OK)
        return RES_OK;

    //保留首错，后续状态查询不能覆盖超时或掉卡原因
    if (s_last_result == SYS_OK)
        s_last_result = result;
    if (result == SYS_ERR_IO || result == SYS_ERR_TIMEOUT ||
        result == SYS_ERR_NOT_FOUND || result == SYS_ERR_PROTOCOL)
        s_fault = true;
    if (result == SYS_ERR_INVALID_ARG)
        return RES_PARERR;
    if (result == SYS_ERR_NOT_FOUND || result == SYS_ERR_INVALID_STATE)
        return RES_NOTRDY;
    return RES_ERROR;
}

//检查整卡读范围与会话状态
static SysResult_t Disk_Check(BYTE drive, LBA_t sector, UINT count)
{
    BspSdmmcCardInfo_t info;
    SysResult_t result;

    if (drive != 0U || count == 0U)
        return SYS_ERR_INVALID_ARG;
    if (!s_configured || s_fault || !SD_Block_IsOwner(SD_BLOCK_CLIENT_FATFS))
        return SYS_ERR_INVALID_STATE;
    result = Disk_Sync();
    if (result != SYS_OK)
        return result;
    result = SD_Block_GetInfo(&info);
    if (result != SYS_OK)
        return result;
    if (sector >= info.logical_block_count || count > info.logical_block_count - sector)
        return SYS_ERR_INVALID_ARG;
    return SYS_OK;
}


//累计FatFS写请求分布，不表示IDMA实际分批次数
static bool Disk_RecordWrite(uint32_t sector, uint32_t count)
{
    uint64_t whole, fraction;

    //统计饱和不能阻止真实写入，只停止继续累计
    if (g_sd_write_debug.write_calls == UINT32_MAX)
    {
        g_sd_write_debug.saturated = 1U;
        return false;
    }
    g_sd_write_debug.write_calls++;
    g_sd_write_debug.in_flight = 1U;
    g_sd_write_debug.requested_sectors += count;
    g_sd_write_debug.last_lba = sector;
    g_sd_write_debug.last_sectors = count;
    if (g_sd_write_debug.min_sectors == 0U || count < g_sd_write_debug.min_sectors)
        g_sd_write_debug.min_sectors = count;
    if (count > g_sd_write_debug.max_sectors)
        g_sd_write_debug.max_sectors = count;

    //精确保留1到64扇区的分布，较大请求不越界写数组
    if (count == 1U)
        g_sd_write_debug.single_calls++;
    else
        g_sd_write_debug.multi_calls++;
    if (count <= FATFS_WRITE_HISTOGRAM_MAX)
        g_sd_write_debug.sectors_histogram[count]++;
    else
        g_sd_write_debug.over64_calls++;

    //分开整数和余数计算，避免总扇区数乘1000时溢出
    whole = g_sd_write_debug.requested_sectors / g_sd_write_debug.write_calls;
    fraction = (g_sd_write_debug.requested_sectors % g_sd_write_debug.write_calls) * 1000U /
        g_sd_write_debug.write_calls;
    if (whole > UINT32_MAX / 1000U || whole * 1000U + fraction > UINT32_MAX)
        g_sd_write_debug.average_sectors_milli = UINT32_MAX;
    else
        g_sd_write_debug.average_sectors_milli = (uint32_t)(whole * 1000U + fraction);
    return true;
}


//记录原有同步检查耗时
static SysResult_t Disk_Sync(void)
{
    uint32_t started = Platform_TimeGetMs();
    SysResult_t result = SD_Block_Sync();
    FatFsDisk_RecordTiming(&g_storage_io_timing.block_sync, Platform_TimeGetMs() - started);
    return result;
}

//累计调用耗时
void FatFsDisk_RecordTiming(volatile FatFsIoTimingSample_t *sample, uint32_t elapsed)
{

    //单写者直接累计，不关中断，不进行日志输出
    if (sample->samples == UINT32_MAX)
        return;
    sample->samples++;
    sample->last_ms = elapsed;
    if (elapsed > sample->max_ms)
        sample->max_ms = elapsed;
    sample->total_ms += elapsed;
    sample->average_ms = (uint32_t)(sample->total_ms / sample->samples);
}
