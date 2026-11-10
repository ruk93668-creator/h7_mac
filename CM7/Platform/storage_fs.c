//单卷文件管理：任务独占、静态句柄及错误恢复，不包含相机业务
#include "storage_fs.h"
#include "fatfs_diskio.h"
#include "sd_block.h"
#include "ff.h"
#include "diskio.h"
#include "framework_init.h"
#include "stm32h7xx.h"
#include <string.h>
#include <stddef.h>

#define STORAGE_FILE_COUNT 4U
#define STORAGE_FS_READY 1U
#define STORAGE_FS_MOUNTED 2U
#define STORAGE_FS_FAULT 3U
#define STORAGE_GENERATION_MAX 0xFFFFFFUL

//文件系统只接收小块内部RAM输入，不直接读取调用者的JPEG SDRAM
#define STORAGE_WRITE_INPUT_BYTES 8192U

//文件对象与凭据仅由卷所有者操作
typedef struct
{
    FIL object;//FatFS私有文件对象
    uint32_t generation;//关闭或重新挂载后不复用旧凭据
    uint32_t mode;//本次打开的读写权限
    bool used;//只有成功打开后才占用槽位
} StorageSlot_t;

//保存唯一FatFS工作区，不使用堆内存
static FATFS s_volume;

//由唯一文件系统任务复用，独立于BSP的32KiB AXI中转和原始图像
static uint8_t s_write_input[STORAGE_WRITE_INPUT_BYTES] __attribute__((aligned(32)));

//保存固定文件池，关闭后仍保留代号避免旧Handle复用
static StorageSlot_t s_files[STORAGE_FILE_COUNT];

//保存外部可读的最近诊断
static StorageFsDiagnostics_t s_diag;

//记录一次完整文件操作，防止同一任务递归进入FatFS
static bool s_busy;

//记录当前模块是否仍持有SD块设备租约
static bool s_lease;

//检查任务、挂载状态并开始一次文件操作
static SysResult_t Storage_Enter(void);

//提前结束本次操作并保存参数或权限错误
static SysResult_t Storage_Leave(SysResult_t result);

//转换FatFS错误并按需要锁存卷故障
static SysResult_t Storage_Finish(FRESULT result);

//查找本轮仍有效的文件凭据
static StorageSlot_t *Storage_Find(StorageFile_t file);

//验证仅使用当前卷的短路径
static bool Storage_PathValid(const char *path);

//读取分区与卷边界，不格式化也不修复介质
static SysResult_t Storage_Inspect(uint8_t requested);

//读取小端32位字段，避免未对齐访问
static uint32_t Storage_Load32(const uint8_t *data);

//使旧文件对象失效，不能把它们重新绑定到新卡
static void Storage_Invalidate(void);

//准备一次静态运行环境
SysResult_t StorageFS_Init(void)
{
    if (__get_IPSR() != 0U)
        return SYS_ERR_INVALID_STATE;
    if (s_diag.state != 0U)
        return SYS_ERR_ALREADY_EXISTS;

    //不在Initcall中识卡、挂载或执行写入
    s_diag.state = STORAGE_FS_READY;
    return SYS_OK;
}

//由当前任务显式挂载已检查的FAT卷
SysResult_t StorageFS_Mount(bool writable, uint8_t partition)
{
    SysResult_t result;
    FRESULT fr;
    SdBlockStatus_t previous;

    if (partition > 4U)
        return SYS_ERR_INVALID_ARG;
    if (s_diag.state == 0U || s_diag.state == STORAGE_FS_FAULT)
        return SYS_ERR_INVALID_STATE;
    if (s_busy)
        return SYS_ERR_BUSY;
    if (s_lease)
        return SD_Block_IsOwner(SD_BLOCK_CLIENT_FATFS) ? SYS_ERR_ALREADY_EXISTS : SYS_ERR_BUSY;

    //先取得块设备租约，MSC或其他任务不能同时进入
    result = SD_Block_Claim(SD_BLOCK_CLIENT_FATFS);
    if (result != SYS_OK)
        return result;
    s_lease = true;
    s_busy = true;
    //清除上一轮卷信息，失败时不能继续展示旧卷为当前卷
    s_diag.partition = partition;
    s_diag.first_lba = 0U;
    s_diag.sector_count = 0U;
    s_diag.filesystem = 0U;
    s_diag.layout = STORAGE_FS_LAYOUT_UNKNOWN;
    s_diag.fatfs_called = 0U;
    s_diag.writable = 0U;
    memset(s_diag.cid, 0, sizeof(s_diag.cid));
    s_diag.last_result = SYS_OK;
    s_diag.cleanup_result = SYS_OK;
    s_diag.block_result = SYS_OK;
    s_diag.fatfs_result = FR_OK;
    if (s_diag.session == UINT32_MAX)
        result = SYS_ERR_NO_RESOURCE;
    else
    {
        //重新挂载不能沿用上轮卡身份，空闲且已初始化时重新识卡
        result = SD_Block_GetStatus(&previous);
        if (result == SYS_OK && previous.busy)
            result = SYS_ERR_BUSY;
        if (result == SYS_OK && (previous.initialized || previous.fault))
            result = SD_Block_Recover();
        if (result == SYS_OK)
            result = SD_Block_Init();
    }
    if (result == SYS_OK)
        result = SD_Block_GetIdentity(s_diag.cid);
    if (result == SYS_OK)
        result = Storage_Inspect(partition);
    if (result == SYS_OK)
        result = FatFsDisk_Configure((uint8_t)s_diag.partition, s_diag.first_lba, s_diag.sector_count);
    if (result == SYS_OK)
    {
        //真正加载卷，挂载检查期间磁盘写入口仍关闭
        FatFsDisk_Begin();
        fr = f_mount(&s_volume, "0:", 1U);
        s_diag.fatfs_result = fr;
        s_diag.fatfs_called = 1U;
        if (fr == FR_OK)
        {
            if (s_volume.volbase != s_diag.first_lba)
                result = SYS_ERR_PROTOCOL;
            else
                result = FatFsDisk_SetWritable(writable);
        }
        else
        {
            //保留FatFS和底层首错，再执行统一失败清理
            result = Storage_Finish(fr);
            s_busy = true;
        }
    }
    if (result == SYS_OK)
    {
        s_diag.session++;
        s_diag.filesystem = s_volume.fs_type;
        s_diag.writable = writable ? 1U : 0U;
        s_diag.state = STORAGE_FS_MOUNTED;
        s_busy = false;
        return SYS_OK;
    }

    //挂载失败只注销本地工作区，不自动格式化或反复写卡
    (void)f_mount(NULL, "0:", 0U);
    FatFsDisk_Reset();
    s_diag.last_result = result;
    if (s_diag.block_result == SYS_OK && (result == SYS_ERR_IO || result == SYS_ERR_TIMEOUT || result == SYS_ERR_NOT_FOUND))
        s_diag.block_result = result;
    s_diag.cleanup_result = SD_Block_Release(SD_BLOCK_CLIENT_FATFS);
    if (s_diag.cleanup_result == SYS_OK)
    {
        s_lease = false;
        s_diag.state = STORAGE_FS_READY;
    }
    else
        s_diag.state = STORAGE_FS_FAULT;
    s_busy = false;
    return result;
}

//卸载已关闭文件的卷
SysResult_t StorageFS_Unmount(void)
{
    SysResult_t result = Storage_Enter();
    if (result != SYS_OK)
        return result;

    //不擅自关闭调用者文件或丢弃未同步内容
    if (s_diag.open_files != 0U)
    {
        return Storage_Leave(SYS_ERR_BUSY);
    }
    result = SD_Block_Sync();
    if (result != SYS_OK)
    {
        s_diag.block_result = result;
        s_diag.last_result = result;
        s_diag.state = STORAGE_FS_FAULT;
        s_busy = false;
        return result;
    }
    (void)f_mount(NULL, "0:", 0U);
    FatFsDisk_Reset();
    result = SD_Block_Release(SD_BLOCK_CLIENT_FATFS);
    s_diag.cleanup_result = result;
    if (result == SYS_OK)
    {
        s_lease = false;
        s_diag.state = STORAGE_FS_READY;
        s_diag.writable = 0U;
    }
    else
        s_diag.state = STORAGE_FS_FAULT;
    s_busy = false;
    return result;
}

//显式恢复设备并撤销旧卷
SysResult_t StorageFS_Recover(void)
{
    SysResult_t result;

    //恢复只能由仍持有故障租约的任务执行
    if (s_busy || !s_lease || !SD_Block_IsOwner(SD_BLOCK_CLIENT_FATFS))
        return SYS_ERR_INVALID_STATE;
    if (s_diag.state != STORAGE_FS_FAULT)
        return SYS_ERR_INVALID_STATE;
    s_busy = true;
    Storage_Invalidate();
    (void)f_mount(NULL, "0:", 0U);
    FatFsDisk_Reset();
    result = SD_Block_Recover();
    if (result == SYS_OK)
        result = SD_Block_Release(SD_BLOCK_CLIENT_FATFS);
    s_diag.cleanup_result = result;
    if (result == SYS_OK)
    {
        s_lease = false;
        s_diag.state = STORAGE_FS_READY;
        s_diag.writable = 0U;
    }
    s_busy = false;
    return result;
}

//打开现有文件或显式新建文件
SysResult_t StorageFS_Open(const char *path, uint32_t mode, StorageFile_t *file)
{
    uint32_t i;
    BYTE flags = 0U;
    FRESULT fr;
    SysResult_t result;

    if (file == NULL || !Storage_PathValid(path) || (mode & ~15U) != 0U)
        return SYS_ERR_INVALID_ARG;
    *file = 0U;
    if ((mode & (STORAGE_FS_OPEN_READ | STORAGE_FS_OPEN_WRITE)) == 0U)
        return SYS_ERR_INVALID_ARG;
    if ((mode & (STORAGE_FS_OPEN_CREATE_NEW | STORAGE_FS_OPEN_APPEND)) != 0U &&
        (mode & STORAGE_FS_OPEN_WRITE) == 0U)
        return SYS_ERR_INVALID_ARG;
    if ((mode & STORAGE_FS_OPEN_CREATE_NEW) != 0U && (mode & STORAGE_FS_OPEN_APPEND) != 0U)
        return SYS_ERR_INVALID_ARG;
    result = Storage_Enter();
    if (result != SYS_OK)
        return result;

    //只读挂载拒绝任何可能创建或改写文件的方式
    if ((mode & STORAGE_FS_OPEN_WRITE) != 0U && s_diag.writable == 0U)
    {
        return Storage_Leave(SYS_ERR_NOT_SUPPORTED);
    }
    for (i = 0U; i < STORAGE_FILE_COUNT; i++)
        if (!s_files[i].used && s_files[i].generation < STORAGE_GENERATION_MAX)
            break;
    if (i == STORAGE_FILE_COUNT)
    {
        return Storage_Leave(SYS_ERR_NO_RESOURCE);
    }
    if ((mode & STORAGE_FS_OPEN_READ) != 0U)
        flags |= FA_READ;
    if ((mode & STORAGE_FS_OPEN_WRITE) != 0U)
        flags |= FA_WRITE;
    if ((mode & STORAGE_FS_OPEN_CREATE_NEW) != 0U)
        flags |= FA_CREATE_NEW;
    if ((mode & STORAGE_FS_OPEN_APPEND) != 0U)
        flags |= FA_OPEN_APPEND;
    fr = f_open(&s_files[i].object, path, flags);
    if (fr == FR_OK)
    {
        //只有FatFS成功后提交新代号，旧Handle不能借用新对象
        s_files[i].generation++;
        s_files[i].mode = mode;
        s_files[i].used = true;
        s_diag.open_files++;
        *file = (s_files[i].generation << 8) | (i + 1U);
    }
    return Storage_Finish(fr);
}

//读取并返回本次实际字节数
SysResult_t StorageFS_Read(StorageFile_t file, void *data, uint32_t size, uint32_t *read_bytes)
{
    StorageSlot_t *slot;
    SysResult_t result;
    FRESULT fr;
    UINT count = 0U;

    if (read_bytes == NULL || (data == NULL && size != 0U))
        return SYS_ERR_INVALID_ARG;
    *read_bytes = 0U;
    result = Storage_Enter();
    if (result != SYS_OK)
        return result;
    slot = Storage_Find(file);
    if (slot == NULL || (slot->mode & STORAGE_FS_OPEN_READ) == 0U)
    {
        return Storage_Leave(SYS_ERR_INVALID_STATE);
    }
    fr = f_read(&slot->object, data, size, &count);
    *read_bytes = count;
    return Storage_Finish(fr);
}

//写入并保留部分进度
SysResult_t StorageFS_Write(StorageFile_t file, const void *data, uint32_t size, uint32_t *written_bytes)
{
    StorageSlot_t *slot;
    SysResult_t result;
    FRESULT fr = FR_OK;
    UINT count = 0U;
    uint32_t done = 0U, amount;

    if (written_bytes == NULL || (data == NULL && size != 0U))
        return SYS_ERR_INVALID_ARG;
    *written_bytes = 0U;
    result = Storage_Enter();
    if (result != SYS_OK)
        return result;
    slot = Storage_Find(file);
    if (slot == NULL || (slot->mode & STORAGE_FS_OPEN_WRITE) == 0U || s_diag.writable == 0U)
    {
        return Storage_Leave(SYS_ERR_INVALID_STATE);
    }

    //FAT文件上限不能通过32位加法回绕绕过
    if (size > UINT32_MAX - f_tell(&slot->object))
    {
        return Storage_Leave(SYS_ERR_NOT_SUPPORTED);
    }

    do
    {
        amount = size - done;
        if (amount > sizeof(s_write_input))
            amount = sizeof(s_write_input);

        //必须在原帧租借有效时完成输入复制，FatFS不再拿到调用者的数据指针
        if (amount != 0U)
        {
            result = BSP_SDMMC_PrepareWriteInput(s_write_input,
                (const uint8_t *)data + done, amount);
            if (result != SYS_OK)
            {

                //保留此前已写入的字节数，不把输入准备失败伪装成FatFS错误
                s_diag.block_result = FatFsDisk_GetLastResult();
                return Storage_Leave(result);
            }
        }

        //同步f_write返回前输入缓冲不复用，包含FatFS内部扇区缓存和底层IDMA路径
        count = 0U;
        fr = f_write(&slot->object, s_write_input, amount, &count);
        done += count;
        *written_bytes = done;
        s_diag.fatfs_called = 1U;
        s_diag.fatfs_result = fr;

        //错误或写满时立即保留部分进度，不提交后续输入块
        if (fr != FR_OK || count != amount)
            break;
    } while (done < size);
    result = Storage_Finish(fr);

    //FR_OK也必须检查实际写入量，写满不是完整成功
    if (result == SYS_OK && done != size)
    {
        s_diag.last_result = SYS_ERR_NO_RESOURCE;
        return SYS_ERR_NO_RESOURCE;
    }
    return result;
}

//同步当前文件
SysResult_t StorageFS_Sync(StorageFile_t file)
{
    StorageSlot_t *slot;
    SysResult_t result = Storage_Enter();
    if (result != SYS_OK)
        return result;
    slot = Storage_Find(file);
    if (slot == NULL)
    {
        return Storage_Leave(SYS_ERR_INVALID_STATE);
    }
    return Storage_Finish(f_sync(&slot->object));
}

//关闭并释放当前文件槽位
SysResult_t StorageFS_Close(StorageFile_t file)
{
    StorageSlot_t *slot;
    FRESULT fr;
    SysResult_t result = Storage_Enter();
    if (result != SYS_OK)
        return result;
    slot = Storage_Find(file);
    if (slot == NULL)
    {
        return Storage_Leave(SYS_ERR_INVALID_STATE);
    }
    fr = f_close(&slot->object);
    if (fr == FR_OK)
    {
        slot->used = false;
        s_diag.open_files--;
    }
    return Storage_Finish(fr);
}

//定位当前文件
SysResult_t StorageFS_Seek(StorageFile_t file, uint32_t offset)
{
    StorageSlot_t *slot;
    FRESULT fr;
    SysResult_t result = Storage_Enter();
    if (result != SYS_OK)
        return result;
    slot = Storage_Find(file);
    if (slot == NULL)
    {
        return Storage_Leave(SYS_ERR_INVALID_STATE);
    }
    if ((slot->mode & STORAGE_FS_OPEN_WRITE) == 0U && offset > f_size(&slot->object))
    {
        return Storage_Leave(SYS_ERR_INVALID_ARG);
    }
    fr = f_lseek(&slot->object, offset);
    s_diag.fatfs_called = 1U;
    s_diag.fatfs_result = fr;
    if (fr == FR_OK && f_tell(&slot->object) != offset)
    {
        s_busy = false;
        s_diag.last_result = SYS_ERR_NO_RESOURCE;
        return SYS_ERR_NO_RESOURCE;
    }
    return Storage_Finish(fr);
}

//读取当前文件长度
SysResult_t StorageFS_GetSize(StorageFile_t file, uint32_t *size)
{
    StorageSlot_t *slot;
    SysResult_t result;
    if (size == NULL)
        return SYS_ERR_INVALID_ARG;
    result = Storage_Enter();
    if (result != SYS_OK)
        return result;
    slot = Storage_Find(file);
    if (slot == NULL)
    {
        return Storage_Leave(SYS_ERR_INVALID_STATE);
    }
    *size = (uint32_t)f_size(&slot->object);
    return Storage_Finish(FR_OK);
}

//显式查询卷可用空间
SysResult_t StorageFS_GetSpace(uint64_t *total_bytes, uint64_t *free_bytes)
{
    FATFS *volume;
    DWORD clusters;
    FRESULT fr;
    SysResult_t result;
    if (total_bytes == NULL || free_bytes == NULL)
        return SYS_ERR_INVALID_ARG;
    result = Storage_Enter();
    if (result != SYS_OK)
        return result;
    fr = f_getfree("0:", &clusters, &volume);
    if (fr == FR_OK)
    {
        *total_bytes = (uint64_t)(volume->n_fatent - 2U) * volume->csize * 512U;
        *free_bytes = (uint64_t)clusters * volume->csize * 512U;
    }
    return Storage_Finish(fr);
}

//创建指定目录
SysResult_t StorageFS_Mkdir(const char *path)
{
    SysResult_t result;
    if (!Storage_PathValid(path))
        return SYS_ERR_INVALID_ARG;
    result = Storage_Enter();
    if (result != SYS_OK)
        return result;
    if (s_diag.writable == 0U)
    {
        return Storage_Leave(SYS_ERR_NOT_SUPPORTED);
    }
    return Storage_Finish(f_mkdir(path));
}

//删除指定文件或空目录
SysResult_t StorageFS_Remove(const char *path)
{
    SysResult_t result;
    if (!Storage_PathValid(path))
        return SYS_ERR_INVALID_ARG;
    result = Storage_Enter();
    if (result != SYS_OK)
        return result;
    if (s_diag.writable == 0U)
    {
        return Storage_Leave(SYS_ERR_NOT_SUPPORTED);
    }
    return Storage_Finish(f_unlink(path));
}

//读取稳定的诊断副本
SysResult_t StorageFS_GetDiagnostics(StorageFsDiagnostics_t *diagnostics)
{
    uint32_t mask;
    if (diagnostics == NULL)
        return SYS_ERR_INVALID_ARG;
    if (__get_IPSR() != 0U)
        return SYS_ERR_INVALID_STATE;
    mask = __get_PRIMASK();
    __disable_irq();
    if (s_busy)
    {
        __set_PRIMASK(mask);
        return SYS_ERR_BUSY;
    }
    *diagnostics = s_diag;
    __set_PRIMASK(mask);
    return SYS_OK;
}

//检查允许执行文件操作的上下文
static SysResult_t Storage_Enter(void)
{
    if (!SD_Block_IsOwner(SD_BLOCK_CLIENT_FATFS) || s_diag.state != STORAGE_FS_MOUNTED)
        return SYS_ERR_INVALID_STATE;
    if (s_busy)
        return SYS_ERR_BUSY;
    s_busy = true;
    s_diag.fatfs_called = 0U;
    s_diag.block_result = SYS_OK;
    FatFsDisk_Begin();

    //即使文件数据已在FatFS缓存中，也先识别当前掉卡或故障
    {
        SysResult_t result = SD_Block_Sync();
        if (result != SYS_OK)
        {
            if (result != SYS_ERR_BUSY)
            {
                s_diag.block_result = result;
                s_diag.last_result = result;
                s_diag.state = STORAGE_FS_FAULT;
                Storage_Invalidate();
            }
            s_busy = false;
            return result;
        }
    }
    return SYS_OK;
}

//提前结束已进入的操作，不伪造FatFS返回值
static SysResult_t Storage_Leave(SysResult_t result)
{
    s_diag.last_result = result;
    s_busy = false;
    return result;
}

//保存错误并完成一次操作
static SysResult_t Storage_Finish(FRESULT fr)
{
    SysResult_t result = SYS_ERR_IO;
    SysResult_t block = FatFsDisk_GetLastResult();

    //文件级错误不一律升级成设备故障
    switch (fr)
    {
        case FR_OK: result = SYS_OK; break;
        case FR_NO_FILE: case FR_NO_PATH: result = SYS_ERR_NOT_FOUND; break;
        case FR_EXIST: result = SYS_ERR_ALREADY_EXISTS; break;
        case FR_INVALID_NAME: case FR_INVALID_PARAMETER: result = SYS_ERR_INVALID_ARG; break;
        case FR_DENIED: case FR_TOO_MANY_OPEN_FILES: result = SYS_ERR_NO_RESOURCE; break;
        case FR_WRITE_PROTECTED: result = SYS_ERR_NOT_SUPPORTED; break;
        case FR_LOCKED: result = SYS_ERR_BUSY; break;
        case FR_TIMEOUT: result = SYS_ERR_TIMEOUT; break;
        case FR_NOT_ENABLED: case FR_INVALID_OBJECT: result = SYS_ERR_INVALID_STATE; break;
        case FR_NO_FILESYSTEM: case FR_INT_ERR: result = SYS_ERR_PROTOCOL; break;
        default: result = block != SYS_OK ? block : SYS_ERR_IO; break;
    }
    s_diag.fatfs_result = fr;
    s_diag.fatfs_called = 1U;
    s_diag.block_result = block;
    s_diag.last_result = result;
    if (fr == FR_DISK_ERR || fr == FR_NOT_READY || fr == FR_INT_ERR || fr == FR_TIMEOUT)
    {
        //不可继续的介质错误使所有旧Handle失效，保持块设备租约
        s_diag.state = STORAGE_FS_FAULT;
        if (s_diag.writable != 0U)
            s_diag.data_may_be_lost = 1U;
        Storage_Invalidate();
    }
    s_busy = false;
    return result;
}

//核对文件代号及槽位
static StorageSlot_t *Storage_Find(StorageFile_t file)
{
    uint32_t index = file & 0xFFU;
    if (index == 0U || index > STORAGE_FILE_COUNT)
        return NULL;
    index--;
    if (!s_files[index].used || s_files[index].generation != (file >> 8))
        return NULL;
    return &s_files[index];
}

//检查只使用当前卷的短ASCII路径
static bool Storage_PathValid(const char *path)
{
    uint32_t i;
    if (path == NULL || strncmp(path, "0:/", 3U) != 0)
        return false;
    for (i = 0U; i < 64U && path[i] != '\0'; i++)
    {
        if ((uint8_t)path[i] < 32U || (uint8_t)path[i] > 126U)
            return false;
        if (path[i] == '.' && path[i + 1U] == '.')
            return false;
    }
    return i > 3U && i < 64U;
}

//读取小端字段
static uint32_t Storage_Load32(const uint8_t *data)
{
    return (uint32_t)data[0] | (uint32_t)data[1] << 8 |
        (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

//只读确认分区和卷的边界
static SysResult_t Storage_Inspect(uint8_t requested)
{
    uint8_t sector[512];
    uint32_t starts[4], lengths[4];
    uint32_t i, j, selected = 4U, total;
    bool direct;
    BspSdmmcCardInfo_t info;
    SysResult_t result = SD_Block_GetInfo(&info);

    if (result != SYS_OK)
        return result;
    result = SD_Block_Read(0U, 1U, sector, 2000U);
    if (result != SYS_OK)
        return result;
    if (memcmp(sector + 3, "EXFAT   ", 8U) == 0)
    {
        s_diag.layout = STORAGE_FS_LAYOUT_EXFAT;
        return SYS_ERR_NOT_SUPPORTED;
    }
    if (sector[510] != 0x55U || sector[511] != 0xAAU)
    {
        s_diag.layout = STORAGE_FS_LAYOUT_INVALID;
        return SYS_ERR_PROTOCOL;
    }

    //无分区表FAT卷直接使用LBA0，仍要交给FatFS校验BPB
    direct = sector[11] == 0U && sector[12] == 2U && sector[13] != 0U &&
        (sector[13] & (sector[13] - 1U)) == 0U && sector[16] >= 1U && sector[16] <= 2U;
    if (direct)
    {
        if (requested != 0U)
            return SYS_ERR_NOT_SUPPORTED;
        s_diag.layout = STORAGE_FS_LAYOUT_DIRECT;
        s_diag.partition = 0U;
        s_diag.first_lba = 0U;
        s_diag.sector_count = info.logical_block_count;
    }
    else
    {
        s_diag.layout = STORAGE_FS_LAYOUT_MBR;

        //检查全部分区边界，不因只挂载第一个卷就忽略畸形表项
        for (i = 0U; i < 4U; i++)
        {
            uint8_t type = sector[446U + i * 16U + 4U];
            starts[i] = Storage_Load32(sector + 446U + i * 16U + 8U);
            lengths[i] = Storage_Load32(sector + 446U + i * 16U + 12U);
            if (type == 0xEEU)
            {
                s_diag.layout = STORAGE_FS_LAYOUT_GPT_MARKER;
                return SYS_ERR_NOT_SUPPORTED;
            }
            if (type == 0U)
            {
                starts[i] = 0U;
                lengths[i] = 0U;
                continue;
            }
            if (starts[i] == 0U || //分区不能覆盖MBR本身
                lengths[i] == 0U || //非空类型必须有实际容量
                starts[i] >= info.logical_block_count || //起点必须在卡内
                lengths[i] > info.logical_block_count - starts[i]) //减法避免末尾回绕
                return SYS_ERR_PROTOCOL;
            if ((requested == 0U || requested == i + 1U) && selected == 4U &&
                (type == 1U || type == 4U || type == 6U || type == 0x0BU || type == 0x0CU || type == 0x0EU))
                selected = i;
        }
        for (i = 0U; i < 4U; i++)
            for (j = i + 1U; j < 4U; j++)
                if (lengths[i] != 0U && lengths[j] != 0U && starts[i] < starts[j] + lengths[j] &&
                    starts[j] < starts[i] + lengths[i])
                    return SYS_ERR_PROTOCOL;
        if (selected == 4U)
            return SYS_ERR_NOT_SUPPORTED;
        s_diag.partition = selected + 1U;
        s_diag.first_lba = starts[selected];
        s_diag.sector_count = lengths[selected];
        result = SD_Block_Read(s_diag.first_lba, 1U, sector, 2000U);
        if (result != SYS_OK)
            return result;
    }

    //分区类型不是文件系统证明，检查卷声明长度后再实际挂载
    if (memcmp(sector + 3, "EXFAT   ", 8U) == 0)
    {
        s_diag.layout = STORAGE_FS_LAYOUT_EXFAT;
        return SYS_ERR_NOT_SUPPORTED;
    }
    if (sector[11] != 0U || sector[12] != 2U || sector[510] != 0x55U || sector[511] != 0xAAU)
        return SYS_ERR_PROTOCOL;
    total = (uint32_t)sector[19] | (uint32_t)sector[20] << 8;
    if (total == 0U)
        total = Storage_Load32(sector + 32);
    if (total < 64U || total > s_diag.sector_count)
        return SYS_ERR_PROTOCOL;
    s_diag.sector_count = total;
    return SYS_OK;
}

//使本会话的文件凭据全部失效
static void Storage_Invalidate(void)
{
    uint32_t i;
    if (s_diag.open_files != 0U)
        s_diag.data_may_be_lost = 1U;
    for (i = 0U; i < STORAGE_FILE_COUNT; i++)
        s_files[i].used = false;
    s_diag.open_files = 0U;
}

FRAMEWORK_INIT_REGISTER(StorageFS_Init, INIT_LEVEL_SERVICE);
