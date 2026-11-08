//可选验收任务只在明确开启测试时创建，不改动业务任务和BOOT计数
#include "sd_block_probe.h"
#include "sd_block.h"
#include "framework_init.h"
#include "FreeRTOS.h"
#include "task.h"
#include "app_tasks.h"
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
            if (g_sd_block_test.restore_required != 0U && request != 3U && request != 4U)
                g_sd_block_test.result = SYS_ERR_INVALID_STATE;
            else if (request == 4U)
            {

                //显式恢复后重新识卡，保留原备份和恢复义务供下一条命令核验
                g_sd_block_test.result = SD_Block_Recover();
                if (g_sd_block_test.result == SYS_OK)
                    g_sd_block_test.result = SD_Block_Init();
            }
            else if (request == 1U)
                g_sd_block_test.result = SdBlockTest_Read();
#if APP_SD_BLOCK_WRITE_TEST_ENABLED
            else if (request == 2U)
                g_sd_block_test.result = SdBlockTest_Write();
            else if (request == 3U)
                g_sd_block_test.result = SdBlockTest_Restore();
#endif
            else
                g_sd_block_test.result = SYS_ERR_NOT_SUPPORTED;

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
