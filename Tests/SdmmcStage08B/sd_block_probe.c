//可选验收任务只在明确开启测试时创建，不改动业务任务和BOOT计数
#include "sd_block_probe.h"
#include "sd_block.h"
#include "framework_init.h"
#include "FreeRTOS.h"
#include "task.h"
#include "app_tasks.h"
#include "platform_time.h"
#include <string.h>

//保存命令和完整诊断，Watch只能在任务空闲时提交下一条命令
volatile SdBlockProbe_t g_sd_block_test;

//准备可选静态验收任务，失败不影响其他初始化
static SysResult_t SdBlockTest_Prepare(void);

#if APP_SD_BLOCK_TEST_ENABLED

//保存验收任务控制块
static StaticTask_t s_test_tcb;

//保存验收任务栈，不从内核堆申请内存
static StackType_t s_test_stack[1024] __attribute__((aligned(8)));

//保存八扇区数据，用于单块和多块读取一致性检查
static uint8_t s_test_data[8U * 512U];

#if APP_SD_BLOCK_STRESS_TEST_ENABLED

//用前后哨兵保护65扇区CPU缓冲区，IDMA仍只访问BSP专用AXI区
typedef struct
{
    uint32_t before;
    uint8_t data[SD_BLOCK_STRESS_BYTES];
    uint32_t after;
} SdBlockStressBuffer_t;

//仅压力镜像分配33280B数据区，正常及写测试镜像不占用它
//仅改变CPU压力缓冲位置，不直接交给IDMA，也不改变读卡和CRC流程
static SdBlockStressBuffer_t s_stress_buffer
    __attribute__((section(".sd_stress_cpu"), zero_init, aligned(32)));

//执行固定范围、有界轮数的只读压力
static SysResult_t SdBlockTest_Stress(void);

//停止压力并汇总本次统计，不改变底层驱动状态
static SysResult_t SdBlockTest_StressFinish(uint32_t started, uint32_t reason, SysResult_t result);
#endif

//分发手工测试命令，保留写恢复义务的优先级
static SysResult_t SdBlockTest_RunRequest(uint32_t request);

//等待BOOT放行后处理Watch命令
static void SdBlockTest_Task(void *argument);

//计算完整读取数据的CRC32
static uint32_t SdBlockTest_Crc(const uint8_t *data, uint32_t size);

//执行只读识卡和一致性验收
static SysResult_t SdBlockTest_Read(void);

#if APP_SD_BLOCK_WRITE_TEST_ENABLED

//保存原始八扇区，异常后保持到恢复完成，不用于未知区域
static uint8_t s_backup[8U * 512U];

//对照用户明确提供的CID、容量和原始区域核对授权
static SysResult_t SdBlockTest_Authorized(void);

//写入测试或恢复前再次检查授权和边界
static SysResult_t SdBlockTest_WriteChecked(const uint8_t *data);

//执行备份、写入、回读比较和原数据恢复
static SysResult_t SdBlockTest_Write(void);

//恢复原始数据并逐字节核对
static SysResult_t SdBlockTest_Restore(void);
#endif
#endif

//准备默认关闭的独立验收入口
static SysResult_t SdBlockTest_Prepare(void)
{
#if APP_SD_BLOCK_TEST_ENABLED
    TaskHandle_t task;

    //测试资源与业务任务计数隔离，不改变Camera、USB、IPC的创建顺序
    task = xTaskCreateStatic(SdBlockTest_Task, "SdTest", 1024U, NULL, 1U, s_test_stack, &s_test_tcb);
    g_sd_block_test.enabled = task != NULL ? 1U : 0U;
    g_sd_block_test.stress.enabled = APP_SD_BLOCK_STRESS_TEST_ENABLED;
    if (task == NULL)
        g_sd_block_test.result = SYS_ERR_NO_RESOURCE;
#endif
    return SYS_OK;
}

#if APP_SD_BLOCK_TEST_ENABLED

//等待手工请求并记录测试结果
static void SdBlockTest_Task(void *argument)
{
    BspSdmmcIoDiagnostics_t io;
    BspSdmmcDiagnostics_t init;
    uint32_t request;
    (void)argument;

    //调度器启动不等于应用准备成功，等待既有BOOT标志后再进行可选测试
    while (g_app_tasks_debug.boot_released == 0U)
        vTaskDelay(pdMS_TO_TICKS(20U));
    for (;;)
    {
        request = g_sd_block_test.request;
        if (request != 0U)
        {
            g_sd_block_test.request = 0U;
            g_sd_block_test.state = 1U;

            //压力或写恢复都在本任务串行执行，不创建另一条业务执行路径
            g_sd_block_test.result = SdBlockTest_RunRequest(request);

            //失败也读取独立诊断，不能以GetDiagnostics成功覆盖真实测试错误
            if (BSP_SDMMC_GetIoDiagnostics(&io) == SYS_OK)
                g_sd_block_test.io = io;
            if (BSP_SDMMC_GetDiagnostics(&init) == SYS_OK)
                g_sd_block_test.init = init;
            g_sd_block_test.stack_free_words = uxTaskGetStackHighWaterMark(NULL);
            g_sd_block_test.state = g_sd_block_test.result == SYS_OK ? 2U : 3U;
        }
        vTaskDelay(pdMS_TO_TICKS(20U));
    }
}

//分发测试命令
static SysResult_t SdBlockTest_RunRequest(uint32_t request)
{
    SysResult_t result;

    //原数据仍待恢复时，禁止开始任何新的读取或压力测试
    if (g_sd_block_test.restore_required != 0U && request != 3U && request != 4U)
        return SYS_ERR_INVALID_STATE;

    //恢复只由手工命令触发，压力失败不会自动走到这里
    if (request == 4U)
    {
        result = SD_Block_Recover();
        return result == SYS_OK ? SD_Block_Init() : result;
    }
    if (request == 1U)
        return SdBlockTest_Read();
#if APP_SD_BLOCK_WRITE_TEST_ENABLED
    if (request == 2U)
        return SdBlockTest_Write();
    if (request == 3U)
        return SdBlockTest_Restore();
#endif
#if APP_SD_BLOCK_STRESS_TEST_ENABLED
    if (request == 5U)
        return SdBlockTest_Stress();
#endif
    return SYS_ERR_NOT_SUPPORTED;
}

//校验只读路径返回的数据一致
static SysResult_t SdBlockTest_Read(void)
{
    BspSdmmcCardInfo_t card;
    uint32_t cid[4], first;
    SysResult_t result;

    //已经初始化允许重复只读，不在正常测试中自动格式化或擦除
    result = SD_Block_Init();
    if (result != SYS_OK)
        return result;
    result = SD_Block_GetInfo(&card);
    if (result != SYS_OK)
        return result;
    result = BSP_SDMMC_GetIdentity(cid);
    if (result != SYS_OK)
        return result;
    g_sd_block_test.card = card;
    memcpy((void *)g_sd_block_test.cid, cid, sizeof(cid));

    //先读单块，再读八块并重读，CRC只表示重复一致，不证明文件系统内容正确
    result = SD_Block_Read(0U, 1U, s_test_data, 2000U);
    if (result != SYS_OK)
        return result;
    first = SdBlockTest_Crc(s_test_data, 512U);
    result = SD_Block_Read(0U, 8U, s_test_data, 2000U);
    if (result != SYS_OK)
        return result;
    if (first != SdBlockTest_Crc(s_test_data, 512U))
        return SYS_ERR_PROTOCOL;
    g_sd_block_test.read_crc = SdBlockTest_Crc(s_test_data, sizeof(s_test_data));
    result = SD_Block_Read(0U, 8U, s_test_data, 2000U);
    if (result != SYS_OK)
        return result;
    g_sd_block_test.repeat_crc = SdBlockTest_Crc(s_test_data, sizeof(s_test_data));
    return g_sd_block_test.read_crc == g_sd_block_test.repeat_crc ? SYS_OK : SYS_ERR_PROTOCOL;
}

//计算CRC32
static uint32_t SdBlockTest_Crc(const uint8_t *data, uint32_t size)
{
    uint32_t crc = UINT32_MAX, i, bit;

    //逐字节处理，无需查表或动态内存
    for (i = 0U; i < size; i++)
    {
        crc ^= data[i];
        for (bit = 0U; bit < 8U; bit++)
            crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? 0xEDB88320UL : 0U);
    }
    return ~crc;
}

#if APP_SD_BLOCK_STRESS_TEST_ENABLED

//执行有界只读压力
static SysResult_t SdBlockTest_Stress(void)
{
    BspSdmmcCardInfo_t card;
    uint32_t cid[4], started, read_started, elapsed, pause, crc;
    SysResult_t result;

    //不能覆盖仍在运行的一轮统计，也不能覆盖需要恢复的原数据
    if (g_sd_block_test.stress.active != 0U || g_sd_block_test.restore_required != 0U)
        return SYS_ERR_BUSY;

    //每次手工启动重新建立CRC基准，不继承上轮停止请求或计数
    memset((void *)&g_sd_block_test.stress, 0, sizeof(g_sd_block_test.stress));
    g_sd_block_test.stress.enabled = 1U;
    g_sd_block_test.stress.active = 1U;
    g_sd_block_test.stress.limit_ms = SD_BLOCK_STRESS_DURATION_MS;
    g_sd_block_test.stress.max_rounds = SD_BLOCK_STRESS_MAX_ROUNDS;
    s_stress_buffer.before = 0xA55A5AA5UL;
    s_stress_buffer.after = 0xA55A5AA5UL;
    started = Platform_TimeGetMs();

    //只准备读卡，初始化失败立即退出，不格式化、不写入、不自动恢复
    result = SD_Block_Init();
    if (result == SYS_OK)
        result = SD_Block_GetInfo(&card);
    if (result == SYS_OK)
        result = BSP_SDMMC_GetIdentity(cid);
    if (result != SYS_OK)
        return SdBlockTest_StressFinish(started, SD_STRESS_STOP_INIT, result);
    if (card.logical_block_size != 512U || card.logical_block_count < SD_BLOCK_STRESS_BLOCKS)
        return SdBlockTest_StressFinish(started, SD_STRESS_STOP_INIT, SYS_ERR_INVALID_ARG);
    g_sd_block_test.card = card;
    memcpy((void *)g_sd_block_test.cid, cid, sizeof(cid));

    for (;;)
    {

        //只使用编译期硬上限，不能通过Watch扩大测试时间或轮数
        elapsed = Platform_TimeGetMs() - started;
        if (elapsed >= SD_BLOCK_STRESS_DURATION_MS)
            return SdBlockTest_StressFinish(started, SD_STRESS_STOP_TIME, SYS_OK);
        if (g_sd_block_test.stress.completed_rounds >= SD_BLOCK_STRESS_MAX_ROUNDS)
            return SdBlockTest_StressFinish(started, SD_STRESS_STOP_ROUNDS, SYS_OK);
        if (g_sd_block_test.stress.stop_request != 0U)
            return SdBlockTest_StressFinish(started, SD_STRESS_STOP_USER, SYS_OK);

        //65扇区自动分为64加1两批，只读取固定LBA0，不访问写接口
        read_started = Platform_TimeGetMs();
        g_sd_block_test.stress.attempted_rounds++;
        result = SD_Block_Read(0U, SD_BLOCK_STRESS_BLOCKS, s_stress_buffer.data,
            SD_BLOCK_STRESS_READ_TIMEOUT);
        elapsed = Platform_TimeGetMs() - read_started;
        g_sd_block_test.stress.last_read_ms = elapsed;
        g_sd_block_test.stress.total_read_ms += elapsed;
        if (elapsed > g_sd_block_test.stress.max_read_ms)
            g_sd_block_test.stress.max_read_ms = elapsed;

        //首次驱动错误保留原结果，绝不恢复后继续跑来掩盖错误
        if (result != SYS_OK)
        {
            g_sd_block_test.stress.io_errors++;
            return SdBlockTest_StressFinish(started, SD_STRESS_STOP_IO, result);
        }

        //在计算CRC前检查边界，拒绝把越界后的数据作为稳定性证据
        if (s_stress_buffer.before != 0xA55A5AA5UL || s_stress_buffer.after != 0xA55A5AA5UL)
        {
            g_sd_block_test.stress.buffer_errors++;
            return SdBlockTest_StressFinish(started, SD_STRESS_STOP_BUFFER, SYS_ERR_PROTOCOL);
        }
        crc = SdBlockTest_Crc(s_stress_buffer.data, sizeof(s_stress_buffer.data));
        g_sd_block_test.stress.last_crc = crc;
        if (g_sd_block_test.stress.crc_ready == 0U)
        {

            //首轮只能作为重复一致性的基准，不证明扇区内容本身正确
            g_sd_block_test.stress.reference_crc = crc;
            g_sd_block_test.stress.crc_ready = 1U;
        }
        else if (crc != g_sd_block_test.stress.reference_crc)
        {
            g_sd_block_test.stress.crc_errors++;
            return SdBlockTest_StressFinish(started, SD_STRESS_STOP_CRC, SYS_ERR_PROTOCOL);
        }
        g_sd_block_test.stress.completed_rounds++;
        g_sd_block_test.stress.bytes_read += sizeof(s_stress_buffer.data);
        g_sd_block_test.stress.elapsed_ms = Platform_TimeGetMs() - started;
        g_sd_block_test.stack_free_words = uxTaskGetStackHighWaterMark(NULL);

        //手工停止等待当前完整读取结束，不中途释放驱动缓冲区
        if (g_sd_block_test.stress.stop_request != 0U)
            return SdBlockTest_StressFinish(started, SD_STRESS_STOP_USER, SYS_OK);
        elapsed = Platform_TimeGetMs() - started;
        if (elapsed < SD_BLOCK_STRESS_DURATION_MS)
        {

            //每轮主动阻塞，最后不足20ms只等待剩余时间，不补发积压请求
            pause = SD_BLOCK_STRESS_DURATION_MS - elapsed;
            if (pause > SD_BLOCK_STRESS_PAUSE_MS)
                pause = SD_BLOCK_STRESS_PAUSE_MS;
            vTaskDelay(pdMS_TO_TICKS(pause));
        }
    }
}

//结束压力并保存统计
static SysResult_t SdBlockTest_StressFinish(uint32_t started, uint32_t reason, SysResult_t result)
{
    uint32_t elapsed = Platform_TimeGetMs() - started;
    uint32_t attempts = g_sd_block_test.stress.attempted_rounds;

    //使用64位中间值计算吞吐，避免累计字节乘1000发生回绕
    g_sd_block_test.stress.elapsed_ms = elapsed;
    g_sd_block_test.stress.average_read_us = attempts != 0U ?
        (uint32_t)((uint64_t)g_sd_block_test.stress.total_read_ms * 1000U / attempts) : 0U;
    g_sd_block_test.stress.bytes_per_second = elapsed != 0U ?
        (uint32_t)((uint64_t)g_sd_block_test.stress.bytes_read * 1000U / elapsed) : 0U;
    g_sd_block_test.stress.stop_reason = reason;
    g_sd_block_test.stress.result = result;
    g_sd_block_test.stress.active = 0U;

    //运行中不接受排队命令，避免错误退出后自动执行恢复或新测试
    g_sd_block_test.request = 0U;
    return result;
}
#endif

#if APP_SD_BLOCK_WRITE_TEST_ENABLED

//核对用户授权的测试卡和独立原始区域
static SysResult_t SdBlockTest_Authorized(void)
{
    uint32_t cid[4];
    static const uint32_t expected[4] = {SD_TEST_CID0, SD_TEST_CID1, SD_TEST_CID2, SD_TEST_CID3};
    BspSdmmcCardInfo_t card;
    SysResult_t result;

    //身份必须来自本次识卡，测试期间不允许热换卡或使用文件系统空闲区代替原始区域
    result = SD_Block_GetInfo(&card);
    if (result != SYS_OK)
        return result;
    result = BSP_SDMMC_GetIdentity(cid);
    if (result != SYS_OK)
        return result;
    if (memcmp(cid, expected, sizeof(cid)) != 0 || card.capacity_bytes != SD_TEST_CAPACITY_BYTES)
        return SYS_ERR_INVALID_ARG;

    //减法验证授权范围，LBA0永远不能进入实板写测试
    if (SD_TEST_WRITE_FIRST_LBA == 0U || SD_TEST_WRITE_FIRST_LBA >= card.logical_block_count ||
        SD_TEST_WRITE_BLOCKS > card.logical_block_count - SD_TEST_WRITE_FIRST_LBA || SD_TEST_WRITE_BLOCKS < 8U)
        return SYS_ERR_INVALID_ARG;
    return SYS_OK;
}

//只向已授权区域的前八扇区写入
static SysResult_t SdBlockTest_WriteChecked(const uint8_t *data)
{
    SysResult_t result = SdBlockTest_Authorized();

    //每一次写入和恢复都必须重新通过身份及范围检查
    if (result != SYS_OK)
        return result;
    return SD_Block_Write(SD_TEST_WRITE_FIRST_LBA, 8U, data, 3000U);
}

//恢复原始扇区并验证
static SysResult_t SdBlockTest_Restore(void)
{
    SysResult_t result;

    //没有有效备份时绝不把未初始化内存写入SD卡
    if (g_sd_block_test.restore_required == 0U)
        return SYS_ERR_INVALID_STATE;
    result = SdBlockTest_WriteChecked(s_backup);
    if (result == SYS_OK)
        result = SD_Block_Read(SD_TEST_WRITE_FIRST_LBA, 8U, s_test_data, 3000U);
    if (result == SYS_OK && memcmp(s_test_data, s_backup, sizeof(s_backup)) != 0)
        result = SYS_ERR_PROTOCOL;
    g_sd_block_test.restore_result = result;
    if (result == SYS_OK)
        g_sd_block_test.restore_required = 0U;
    return result;
}

//执行受控写回验收
static SysResult_t SdBlockTest_Write(void)
{
    SysResult_t result, restore;
    uint32_t i;

    //先完成识卡和授权，再读取原始数据到独立备份
    result = SD_Block_Init();
    if (result == SYS_OK)
        result = SdBlockTest_Authorized();
    if (result == SYS_OK)
        result = SD_Block_Read(SD_TEST_WRITE_FIRST_LBA, 8U, s_backup, 3000U);
    if (result != SYS_OK)
        return result;
    for (i = 0U; i < sizeof(s_test_data); i++)
        s_test_data[i] = (uint8_t)(i ^ 0xA5U);

    //写入开始前就记录恢复义务，失败也可能已修改部分扇区
    g_sd_block_test.restore_required = 1U;
    result = SdBlockTest_WriteChecked(s_test_data);
    if (result == SYS_OK)
        result = SD_Block_Read(SD_TEST_WRITE_FIRST_LBA, 8U, s_test_data, 3000U);
    if (result == SYS_OK)
    {
        for (i = 0U; i < sizeof(s_test_data); i++)
        {
            if (s_test_data[i] != (uint8_t)(i ^ 0xA5U))
            {
                result = SYS_ERR_PROTOCOL;
                break;
            }
        }
    }

    //保留原测试失败，恢复失败单独记录，不能假装原数据已经恢复
    restore = SdBlockTest_Restore();
    return result != SYS_OK ? result : restore;
}
#endif
#endif

FRAMEWORK_INIT_REGISTER(SdBlockTest_Prepare, INIT_LEVEL_APPLICATION);
