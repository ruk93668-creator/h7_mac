#ifndef SD_BLOCK_PROBE_H
#define SD_BLOCK_PROBE_H

#include "bsp_sdmmc.h"

//默认不创建验收任务，也不初始化或访问卡
#ifndef APP_SD_BLOCK_TEST_ENABLED
#define APP_SD_BLOCK_TEST_ENABLED 0
#endif

//真实写卡必须单独开启，并完整填写测试卡身份和独立原始区域
#ifndef APP_SD_BLOCK_WRITE_TEST_ENABLED
#define APP_SD_BLOCK_WRITE_TEST_ENABLED 0
#endif

//只读压力测试默认关闭，开启时不能同时包含真实写测试
#ifndef APP_SD_BLOCK_STRESS_TEST_ENABLED
#define APP_SD_BLOCK_STRESS_TEST_ENABLED 0
#endif

//D组只禁止创建测试任务，压力代码和静态缓冲仍保留
#ifndef APP_SD_BLOCK_D_GROUP_ENABLED
#define APP_SD_BLOCK_D_GROUP_ENABLED 0
#endif

#if APP_SD_BLOCK_D_GROUP_ENABLED != 0 && APP_SD_BLOCK_D_GROUP_ENABLED != 1
#error SD_block_d_group_switch_must_be_zero_or_one
#endif

#if APP_SD_BLOCK_D_GROUP_ENABLED && (!APP_SD_BLOCK_TEST_ENABLED || !APP_SD_BLOCK_STRESS_TEST_ENABLED || APP_SD_BLOCK_WRITE_TEST_ENABLED)
#error SD_block_d_group_requires_read_stress_configuration
#endif

//轮询对照模式只观察调度，拒绝所有SD测试请求
#ifndef APP_SD_BLOCK_POLL_ONLY_ENABLED
#define APP_SD_BLOCK_POLL_ONLY_ENABLED 0
#endif

#if APP_SD_BLOCK_POLL_ONLY_ENABLED != 0 && APP_SD_BLOCK_POLL_ONLY_ENABLED != 1
#error SD_block_poll_only_switch_must_be_zero_or_one
#endif

#if APP_SD_BLOCK_POLL_ONLY_ENABLED && (!APP_SD_BLOCK_TEST_ENABLED || !APP_SD_BLOCK_STRESS_TEST_ENABLED || APP_SD_BLOCK_WRITE_TEST_ENABLED || APP_SD_BLOCK_D_GROUP_ENABLED)
#error SD_block_poll_only_requires_running_read_stress_task
#endif

//空闲轮询保持原20ms默认值，Watch最多放慢到1000ms
#define SD_BLOCK_POLL_DEFAULT_MS 20U
#define SD_BLOCK_POLL_MIN_MS     20U
#define SD_BLOCK_POLL_MAX_MS     1000U

//最长五分钟、最多一万五千轮，测试可缩短但不能扩大边界
#ifndef SD_BLOCK_STRESS_DURATION_MS
#define SD_BLOCK_STRESS_DURATION_MS 300000UL
#endif

#ifndef SD_BLOCK_STRESS_MAX_ROUNDS
#define SD_BLOCK_STRESS_MAX_ROUNDS 15000UL
#endif

//65扇区覆盖内部32KiB缓冲区的64加1分批路径
#define SD_BLOCK_STRESS_BLOCKS       65U
#define SD_BLOCK_STRESS_BYTES        (SD_BLOCK_STRESS_BLOCKS * 512U)
#define SD_BLOCK_STRESS_PAUSE_MS     20U
#define SD_BLOCK_STRESS_READ_TIMEOUT 2000U

#if APP_SD_BLOCK_STRESS_TEST_ENABLED != 0 && APP_SD_BLOCK_STRESS_TEST_ENABLED != 1
#error SD_block_stress_switch_must_be_zero_or_one
#endif

#if APP_SD_BLOCK_STRESS_TEST_ENABLED && (!APP_SD_BLOCK_TEST_ENABLED || APP_SD_BLOCK_WRITE_TEST_ENABLED)
#error stress_test_requires_probe_and_disables_write_test
#endif

#if SD_BLOCK_STRESS_DURATION_MS == 0 || SD_BLOCK_STRESS_DURATION_MS > 300000UL || \
    SD_BLOCK_STRESS_MAX_ROUNDS == 0 || SD_BLOCK_STRESS_MAX_ROUNDS > 15000UL
#error SD_block_stress_limits_out_of_range
#endif

//压力停止原因独立于result，手动停止不代表五分钟验收完成
#define SD_STRESS_STOP_NONE    0U
#define SD_STRESS_STOP_TIME    1U
#define SD_STRESS_STOP_ROUNDS  2U
#define SD_STRESS_STOP_USER    3U
#define SD_STRESS_STOP_IO      4U
#define SD_STRESS_STOP_CRC     5U
#define SD_STRESS_STOP_INIT    6U
#define SD_STRESS_STOP_BUFFER  7U

//只记录SD测试自己的统计，不越权读取业务任务状态快照
typedef struct
{
    uint32_t enabled;//本镜像是否编入压力入口
    uint32_t active;//当前是否正在运行压力测试
    uint32_t stop_request;//Watch置1后，在本次安全读取结束后停止
    uint32_t stop_reason;//区分到时、轮数、手动停止和真实错误
    uint32_t limit_ms;//只显示编译期时限，修改此字段不会改变控制边界
    uint32_t max_rounds;//只显示编译期上限，修改此字段不会扩大测试
    uint32_t attempted_rounds;//已发起的完整65扇区请求次数
    uint32_t completed_rounds;//读取成功且CRC一致的轮数，包含首轮基准
    uint32_t io_errors;//SD接口返回错误的次数，首次错误立即停止
    uint32_t crc_errors;//与本轮首个成功样本不一致的次数
    uint32_t buffer_errors;//CPU缓冲区前后哨兵不匹配次数
    uint32_t bytes_read;//已经完成CRC检查的累计字节数
    uint32_t elapsed_ms;//包含识卡、复制、CRC和轮间等待的总时间
    uint32_t last_read_ms;//最近一次SD_Block_Read耗时，不含CRC和轮间等待
    uint32_t max_read_ms;//本轮最长单次读取时间
    uint32_t total_read_ms;//所有已返回读取请求的耗时总和
    uint32_t average_read_us;//按毫秒计时换算的平均值，不代表微秒测量精度
    uint32_t bytes_per_second;//包含CRC和等待的平均校验吞吐，不是卡最高速度
    uint32_t reference_crc;//首个成功读取的CRC，本轮内固定
    uint32_t last_crc;//最近一次完整读取的CRC
    uint32_t crc_ready;//首轮基准是否已经建立
    SysResult_t result;//保留本次真实结果，不自动恢复后重试
} SdBlockStress_t;

#if (APP_SD_BLOCK_TEST_ENABLED != 0 && APP_SD_BLOCK_TEST_ENABLED != 1) || \
    (APP_SD_BLOCK_WRITE_TEST_ENABLED != 0 && APP_SD_BLOCK_WRITE_TEST_ENABLED != 1)
#error SD_block_test_switches_must_be_zero_or_one
#endif

#if APP_SD_BLOCK_WRITE_TEST_ENABLED
#if !APP_SD_BLOCK_TEST_ENABLED
#error write_test_requires_probe_enabled
#endif
#if !defined(SD_TEST_CID0) || !defined(SD_TEST_CID1) || !defined(SD_TEST_CID2) || !defined(SD_TEST_CID3) || \
    !defined(SD_TEST_CAPACITY_BYTES) || !defined(SD_TEST_WRITE_FIRST_LBA) || !defined(SD_TEST_WRITE_BLOCKS)
#error write_test_requires_explicit_card_identity_capacity_and_raw_region
#endif
#if SD_TEST_WRITE_FIRST_LBA == 0 || SD_TEST_WRITE_BLOCKS < 8
#error write_test_must_not_include_LBA0_and_requires_eight_reserved_blocks
#endif
#endif

//保存受控验收请求和结果，不作为正式存储服务
//request=1只读，2授权写入，3恢复原数据，4恢复外设并重新识卡
//2和3仅在写测试启用时支持，4不会自动执行写入或丢弃原数据备份
//request=5启动有界只读压力，运行中只用stress.stop_request请求停止
typedef struct
{
    uint32_t enabled;
    uint32_t request;
    uint32_t state;//0未运行，1处理中，2成功，3失败
    uint32_t read_crc;
    uint32_t repeat_crc;
    uint32_t restore_required;
    SysResult_t result;
    SysResult_t restore_result;
    uint32_t cid[4];
    BspSdmmcCardInfo_t card;
    BspSdmmcDiagnostics_t init;
    BspSdmmcIoDiagnostics_t io;
    uint32_t stack_free_words;
    SdBlockStress_t stress;//追加压力统计，原有Watch字段顺序不变
    uint32_t poll_period_ms;//Watch设置下一轮等待周期，不重启任务
    uint32_t poll_effective_ms;//实际采用的周期，限制在20到1000ms
    uint32_t poll_count;//已进入轮间等待的次数，用增量观察唤醒频率
} SdBlockProbe_t;

//供Keil Watch观察，默认不开启扇区测试
extern volatile SdBlockProbe_t g_sd_block_test;

#if APP_SD_BLOCK_TEST_ENABLED

//只读启动开关，D组为1；不是运行中创建或停止任务的控制接口
extern const volatile uint32_t g_sd_block_d_group;

//当前为1时所有测试请求均拒绝，确保调度对照不读写SD
extern const volatile uint32_t g_sd_block_poll_only;
#endif

#endif
