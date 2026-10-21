#include "sdram_check.h"
#include "bsp_sdram.h"
#include "framework_init.h"
#include "stm32h7xx_hal.h"

#define SDRAM_CHECK_WORDS (SDRAM_CHECK_BYTES / sizeof(uint32_t))

#if SDRAM_CHECK_BYTES > BSP_SDRAM_WINDOW || (SDRAM_CHECK_BYTES % 4U) != 0U
#error Invalid SDRAM check range
#endif

static uint32_t s_backup[SDRAM_CHECK_WORDS];

volatile uint32_t g_sdram_check_state;
volatile SysResult_t g_sdram_check_result = SYS_ERR_INVALID_STATE;
volatile uint32_t g_sdram_check_words;
volatile uint32_t g_sdram_check_fail_address;
volatile uint32_t g_sdram_check_expected;
volatile uint32_t g_sdram_check_actual;

static bool SdramCheck_Compare(
	volatile uint32_t *address,
	uint32_t expected
)
{
	uint32_t actual = *address;

	g_sdram_check_words++;
	if ( actual == expected)
		return true;

	//保留首次失败位置
	if ( g_sdram_check_result == SYS_OK)
	{
		g_sdram_check_fail_address = (uint32_t)address;
		g_sdram_check_expected = expected;
		g_sdram_check_actual = actual;
		g_sdram_check_result = SYS_ERR_IO;
	}
	return false;
}

//独立的小范围验收，成功和失败都恢复原内容
SysResult_t SDRAM_CheckRun(void)
{
	volatile uint32_t *memory = (volatile uint32_t *)BSP_SDRAM_BASE;
	static const uint32_t patterns[] = {0U, 0xFFFFFFFFUL, 0xAAAAAAAAUL, 0x55555555UL};
	uint32_t pattern;
	uint32_t i;
	uint32_t value;

	if ( g_sdram_check_state == 1U)
		return SYS_ERR_BUSY;
	if ( !BSP_SDRAM_IsReady())
		return SYS_ERR_INVALID_STATE;

	g_sdram_check_state = 1U;
	g_sdram_check_result = SYS_OK;
	g_sdram_check_words = 0U;
	g_sdram_check_fail_address = 0U;
	g_sdram_check_expected = 0U;
	g_sdram_check_actual = 0U;

	//只备份首1KB，不访问64KB窗口之外
	for (i = 0U;
		 i < SDRAM_CHECK_WORDS;
		 i++)
		s_backup[i] = memory[i];

	//检查固定图案、地址图案及其反码
	for (pattern = 0U;
		 pattern < 6U;
		 pattern++)
	{
		for (i = 0U;
			 i < SDRAM_CHECK_WORDS;
			 i++)
		{
			value = pattern < 4U ? patterns[pattern] : (uint32_t)&memory[i];
			memory[i] = pattern == 5U ? ~value : value;
		}
		__DSB();
		for (i = 0U;
			 i < SDRAM_CHECK_WORDS;
			 i++)
		{
			value = pattern < 4U ? patterns[pattern] : (uint32_t)&memory[i];
			if ( !SdramCheck_Compare(&memory[i], pattern == 5U ? ~value : value))
				break;
		}
		if ( g_sdram_check_result != SYS_OK)
			break;
	}

	//恢复原数据并检查恢复结果
	for (i = 0U;
		 i < SDRAM_CHECK_WORDS;
		 i++)
		memory[i] = s_backup[i];
	__DSB();
	for (i = 0U;
		 i < SDRAM_CHECK_WORDS;
		 i++)
		SdramCheck_Compare(&memory[i], s_backup[i]);

	g_sdram_check_state = g_sdram_check_result == SYS_OK ? 2U : 3U;
	return g_sdram_check_result;
}

//BUS建立SDRAM后，在MEDIA阶段执行一次1KB验收
FRAMEWORK_INIT_REGISTER(
	SDRAM_CheckRun,
	INIT_LEVEL_MEDIA
);
