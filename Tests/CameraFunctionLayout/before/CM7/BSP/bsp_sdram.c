#include "bsp_sdram.h"
#include "framework_init.h"
#include "stm32h7xx_hal.h"

#define SDRAM_REFRESH_COUNT   839U
#define SDRAM_COMMAND_TIMEOUT 100U

static SDRAM_HandleTypeDef s_sdram;
static bool s_sdram_ready;
static bool s_clock_ready;
static bool s_msp_ready;

//独占PLL2，不改动PLL1和摄像头XCLK
static SysResult_t Sdram_ConfigClock(void)
{
	RCC_PeriphCLKInitTypeDef clock = {0};
	PLL2_ClocksTypeDef clocks;
	HAL_StatusTypeDef status;
	uint32_t frequency;

	//PLL共用时钟源必须保持HSI 64MHz
	if ( (RCC->PLLCKSELR & RCC_PLLCKSELR_PLLSRC) != RCC_PLLSOURCE_HSI ||
		(RCC->CR & RCC_CR_HSIDIV) != 0U ||
		__HAL_RCC_GET_FLAG(RCC_FLAG_HSIRDY) == RESET)
		return SYS_ERR_INVALID_STATE;

	if ( !s_clock_ready)
	{
		//已有模块占用PLL2时拒绝重配
		if ( (RCC->CR & RCC_CR_PLL2ON) != 0U)
			return SYS_ERR_BUSY;

		//64MHz /16 x165 /3 = FMC内核220MHz
		clock.PeriphClockSelection = RCC_PERIPHCLK_FMC | RCC_PERIPHCLK_PLL2_DIVR;
		clock.FmcClockSelection = RCC_FMCCLKSOURCE_PLL2;
		clock.PLL2.PLL2M = 16U;
		clock.PLL2.PLL2N = 165U;
		clock.PLL2.PLL2P = 6U;
		clock.PLL2.PLL2Q = 6U;
		clock.PLL2.PLL2R = 3U;
		clock.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_2;
		clock.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
		clock.PLL2.PLL2FRACN = 0U;

		status = HAL_RCCEx_PeriphCLKConfig(&clock);
		if ( status != HAL_OK)
			return status == HAL_TIMEOUT ? SYS_ERR_TIMEOUT : SYS_ERR_IO;
		s_clock_ready = true;
	}

	//检查实际选择的FMC时钟，SDCLK再分频2
	if ( __HAL_RCC_GET_FMC_SOURCE() != RCC_FMCCLKSOURCE_PLL2)
		return SYS_ERR_INVALID_STATE;
	//当前HAL不支持查询FMC频率，读取PLL2R输出
	HAL_RCCEx_GetPLL2ClockFreq(&clocks);
	frequency = clocks.PLL2_R_Frequency;
	if ( frequency != 220000000UL)
		return SYS_ERR_INVALID_STATE;
	return SYS_OK;
}

//V0引脚保持旧工程配置，不操作GPIOB和摄像头复位脚
void HAL_SDRAM_MspInit(SDRAM_HandleTypeDef *hsdram)
{
	GPIO_InitTypeDef gpio = {0};

	if ( hsdram->Instance != FMC_SDRAM_DEVICE)
		return;

	__HAL_RCC_GPIOD_CLK_ENABLE();
	__HAL_RCC_GPIOE_CLK_ENABLE();
	__HAL_RCC_GPIOF_CLK_ENABLE();
	__HAL_RCC_GPIOG_CLK_ENABLE();
	__HAL_RCC_GPIOH_CLK_ENABLE();
	__HAL_RCC_FMC_CLK_ENABLE();

	gpio.Mode = GPIO_MODE_AF_PP;
	gpio.Pull = GPIO_NOPULL;
	gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
	gpio.Alternate = GPIO_AF12_FMC;

	//D0-D3、D13-D15
	gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_8 | GPIO_PIN_9 |
		GPIO_PIN_10 | GPIO_PIN_14 | GPIO_PIN_15;
	HAL_GPIO_Init(GPIOD, &gpio);

	//NBL0-NBL1、D4-D12
	gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_7 | GPIO_PIN_8 |
		GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 |
		GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
	HAL_GPIO_Init(GPIOE, &gpio);

	//A0-A9、SDNRAS
	gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
		GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_11 | GPIO_PIN_12 |
		GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
	HAL_GPIO_Init(GPIOF, &gpio);

	//A10-A12、BA0-BA1、SDCLK、SDNCAS
	gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_4 |
		GPIO_PIN_5 | GPIO_PIN_8 | GPIO_PIN_15;
	HAL_GPIO_Init(GPIOG, &gpio);

	//SDNWE、SDNE1、SDCKE1
	gpio.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
	HAL_GPIO_Init(GPIOH, &gpio);
	s_msp_ready = true;
}

static SysResult_t Sdram_SendCommand(
	uint32_t mode,
	uint32_t count,
	uint32_t mode_register
)
{
	FMC_SDRAM_CommandTypeDef command = {0};
	HAL_StatusTypeDef status;

	command.CommandMode = mode;
	command.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK2;
	command.AutoRefreshNumber = count;
	command.ModeRegisterDefinition = mode_register;
	status = HAL_SDRAM_SendCommand(&s_sdram, &command, SDRAM_COMMAND_TIMEOUT);
	if ( status != HAL_OK)
		return status == HAL_TIMEOUT ? SYS_ERR_TIMEOUT : SYS_ERR_IO;
	return SYS_OK;
}

static SysResult_t Sdram_Init(void)
{
	FMC_SDRAM_TimingTypeDef timing = {0};
	SysResult_t result;
	HAL_StatusTypeDef status;

	if ( s_sdram_ready)
		return SYS_OK;

	result = Sdram_ConfigClock();
	if ( result != SYS_OK)
		return result;

	//V0为Bank2、10列13行、16位、4个内部Bank
	s_sdram.Instance = FMC_SDRAM_DEVICE;
	s_sdram.Init.SDBank = FMC_SDRAM_BANK2;
	s_sdram.Init.ColumnBitsNumber = FMC_SDRAM_COLUMN_BITS_NUM_10;
	s_sdram.Init.RowBitsNumber = FMC_SDRAM_ROW_BITS_NUM_13;
	s_sdram.Init.MemoryDataWidth = FMC_SDRAM_MEM_BUS_WIDTH_16;
	s_sdram.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
	s_sdram.Init.CASLatency = FMC_SDRAM_CAS_LATENCY_2;
	s_sdram.Init.WriteProtection = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
	s_sdram.Init.SDClockPeriod = FMC_SDRAM_CLOCK_PERIOD_2;
	s_sdram.Init.ReadBurst = FMC_SDRAM_RBURST_DISABLE;
	s_sdram.Init.ReadPipeDelay = FMC_SDRAM_RPIPE_DELAY_0;

	//沿用110MHz实板通过的保守时序
	timing.LoadToActiveDelay = 2U;
	timing.ExitSelfRefreshDelay = 10U;
	timing.SelfRefreshTime = 6U;
	timing.RowCycleDelay = 9U;
	timing.WriteRecoveryTime = 3U;
	timing.RPDelay = 3U;
	timing.RCDDelay = 3U;

	status = HAL_SDRAM_Init(&s_sdram, &timing);
	if ( status != HAL_OK)
		return status == HAL_TIMEOUT ? SYS_ERR_TIMEOUT : SYS_ERR_IO;
	if ( !s_msp_ready)
		return SYS_ERR_INVALID_STATE;

	//使能时钟，等待至少100us
	result = Sdram_SendCommand(FMC_SDRAM_CMD_CLK_ENABLE, 1U, 0U);
	if ( result != SYS_OK)
		return result;
	HAL_Delay(1U);

	//全部预充电、8次自动刷新、加载模式寄存器
	result = Sdram_SendCommand(FMC_SDRAM_CMD_PALL, 1U, 0U);
	if ( result != SYS_OK)
		return result;
	result = Sdram_SendCommand(FMC_SDRAM_CMD_AUTOREFRESH_MODE, 8U, 0U);
	if ( result != SYS_OK)
		return result;
	result = Sdram_SendCommand(FMC_SDRAM_CMD_LOAD_MODE, 1U, 0x0220U);
	if ( result != SYS_OK)
		return result;

	status = HAL_SDRAM_ProgramRefreshRate(&s_sdram, SDRAM_REFRESH_COUNT);
	if ( status != HAL_OK)
		return status == HAL_TIMEOUT ? SYS_ERR_TIMEOUT : SYS_ERR_IO;
	s_sdram_ready = true;
	return SYS_OK;
}

SysResult_t BSP_SDRAM_Init(void)
{
	return Sdram_Init();
}

bool BSP_SDRAM_IsReady(void)
{
	return s_sdram_ready;
}

//系统时钟完成后，在设备初始化前建立SDRAM
FRAMEWORK_INIT_REGISTER(
	BSP_SDRAM_Init,
	INIT_LEVEL_BUS
);
