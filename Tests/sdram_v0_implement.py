from pathlib import Path
import json,hashlib,re,shutil
r=Path('.');t=r/'Tests/SdramV0';t.mkdir(exist_ok=True)
paths=[p for d in ['CM7','CM4','Common','Board'] for p in (r/d).rglob('*') if p.is_file()]+list((r/'MDK-ARM').glob('*.sct'))+[r/'MDK-ARM/h7_mac.uvprojx']
(t/'baseline.json').write_text(json.dumps({str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},indent=2),encoding='utf-8')
for f in ['CM7/Core/Src/main.c','CM7/Core/Inc/stm32h7xx_hal_conf.h','MDK-ARM/h7_mac.uvprojx']:
 dst=t/'before'/f;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(f,dst)
shutil.copyfile('D:/codex_proe/h7_codex/h7_mac/CM7/Core/Src/sdram_test.c',t/'v0_reference.c')
def write(f,s):
 p=Path(f);p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(s.replace('\n','\r\n').encode('gbk'))
write('CM7/BSP/bsp_sdram.h','''#ifndef CM7_BSP_SDRAM_H
#define CM7_BSP_SDRAM_H

#include "sys_result.h"
#include <stdbool.h>

//仅开放Bank2起始64KB，不代表整片可供业务使用
#define BSP_SDRAM_BASE       0xD0000000UL
#define BSP_SDRAM_WINDOW     0x00010000UL
#define BSP_SDRAM_SDCLK_HZ   110000000UL

//供调试器查看初始化结果和实际计算频率
extern volatile SysResult_t g_sdram_init_result;
extern volatile uint32_t g_sdram_sdclk_hz;

SysResult_t BSP_SDRAM_Init(void);
bool BSP_SDRAM_IsReady(void);

#endif
''')
write('CM7/BSP/bsp_sdram.c','''#include "bsp_sdram.h"
#include "framework_init.h"
#include "stm32h7xx_hal.h"

#define SDRAM_REFRESH_COUNT   839U
#define SDRAM_COMMAND_TIMEOUT 100U

static SDRAM_HandleTypeDef s_sdram;
static bool s_sdram_ready;
static bool s_clock_ready;
static bool s_msp_ready;

volatile SysResult_t g_sdram_init_result = SYS_ERR_INVALID_STATE;
volatile uint32_t g_sdram_sdclk_hz;

//独占PLL2，不改动PLL1和摄像头XCLK
static SysResult_t Sdram_ConfigClock(void)
{
	RCC_PeriphCLKInitTypeDef clock = {0};
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
	frequency = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_FMC);
	g_sdram_sdclk_hz = frequency / 2U;
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
	g_sdram_init_result = Sdram_Init();
	return g_sdram_init_result;
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
''')
write('CM7/Diagnostics/sdram_check.h','''#ifndef CM7_SDRAM_CHECK_H
#define CM7_SDRAM_CHECK_H

#include "sys_result.h"

//首1KB保留为验收区，不交给业务缓存
#define SDRAM_CHECK_BYTES 1024U

//0未运行、1运行中、2通过、3失败
extern volatile uint32_t g_sdram_check_state;
extern volatile SysResult_t g_sdram_check_result;
extern volatile uint32_t g_sdram_check_words;
extern volatile uint32_t g_sdram_check_fail_address;
extern volatile uint32_t g_sdram_check_expected;
extern volatile uint32_t g_sdram_check_actual;

SysResult_t SDRAM_CheckRun(void);

#endif
''')
write('CM7/Diagnostics/sdram_check.c','''#include "sdram_check.h"
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
				goto restore;
		}
	}
restore:
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
''')
# Keep custom MPU setup within CubeMX user sections; original MPU regions and boot sequence stay intact.
p=r/'CM7/Core/Src/main.c';s=p.read_bytes().decode('gbk').replace('\r\n','\n');s=s.replace('#include "bsp_camera.h"','#include "bsp_camera.h"\n#include "bsp_sdram.h"')
mark='/* USER CODE BEGIN 0 */\n'
helper='''//Region2覆盖默认禁止区，仅开放SDRAM首64KB
static void MPU_ConfigSdram(void)
{
	MPU_Region_InitTypeDef region = {0};

	HAL_MPU_Disable();
	region.Enable = MPU_REGION_ENABLE;
	region.Number = MPU_REGION_NUMBER2;
	region.BaseAddress = BSP_SDRAM_BASE;
	region.Size = MPU_REGION_SIZE_64KB;
	region.SubRegionDisable = 0U;
	region.TypeExtField = MPU_TEX_LEVEL1;
	region.AccessPermission = MPU_REGION_FULL_ACCESS;
	region.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
	region.IsShareable = MPU_ACCESS_SHAREABLE;
	region.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
	region.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
	HAL_MPU_ConfigRegion(&region);
	HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

'''
assert s.count(mark)==1;s=s.replace(mark,mark+helper)
s=s.replace('/* USER CODE BEGIN SysInit */\n','/* USER CODE BEGIN SysInit */\n\tMPU_ConfigSdram();\n');write(p,s)
# Enable HAL SDRAM in a standalone build config so CubeMX regeneration cannot remove it.
write('CM7/BSP/bsp_sdram_hal_config.h','''#ifndef CM7_BSP_SDRAM_HAL_CONFIG_H
#define CM7_BSP_SDRAM_HAL_CONFIG_H

//由CM7编译器强制包含，避免CubeMX覆盖模块选择
#ifndef HAL_SDRAM_MODULE_ENABLED
#define HAL_SDRAM_MODULE_ENABLED
#endif

#endif
''')
# HAL conf commented out macro doesn't undef preincluded define.
p=r/'MDK-ARM/h7_mac.uvprojx';s=p.read_bytes().decode('utf-8')
# use --preinclude only CM7 compiler MiscControls existing parameter location verified below
root=__import__('xml.etree.ElementTree',fromlist=['ElementTree']).fromstring(s)
for target in root.findall('Targets/Target'):
 if target.findtext('TargetName')=='h7_mac_CM7':
  c=target.find('TargetOption/TargetArmAds/Cads/VariousControls');print('CM7 compiler MiscControls:',c.findtext('MiscControls'))
# add SDRAM to both mirrored CM7 BSP groups, already excluded in CM4
block=''
for f in ['bsp_sdram.c','bsp_sdram.h','bsp_sdram_hal_config.h']:
 block+='\n            <File>\n              <FileName>'+f+'</FileName>\n              <FileType>'+('1' if f.endswith('.c') else '5')+'</FileType>\n              <FilePath>../CM7/BSP/'+f+'</FilePath>\n            </File>'
pattern=r'(<File>\s*<FileName>bsp_dcmi.h</FileName>.*?<FilePath>../CM7/BSP/bsp_dcmi.h</FilePath>\s*</File>)'
s,n=re.subn(pattern,lambda m:m.group(1)+block,s,flags=re.S);assert n==2
# Add separate diagnostics group to each target, excluded on CM4.
parts=s.split('<Target>')
for i in range(1,len(parts)):
 part=parts[i];cm7='<TargetName>h7_mac_CM7</TargetName>' in part
 group='\n        <Group>\n          <GroupName>CM7\\Diagnostics</GroupName>\n'
 if not cm7:group+='          <GroupOption><CommonProperty><IncludeInBuild>0</IncludeInBuild></CommonProperty></GroupOption>\n'
 group+='          <Files>\n'
 for f in ['sdram_check.c','sdram_check.h']:
  group+='            <File><FileName>'+f+'</FileName><FileType>'+('1' if f.endswith('.c') else '5')+'</FileType><FilePath>../CM7/Diagnostics/'+f+'</FilePath></File>\n'
 group+='          </Files>\n        </Group>\n'
 part=part.replace('</Groups>',group+'      </Groups>',1)
 if cm7:
  part=part.replace('..\\CM7\\Device\\Components\\OV5640</IncludePath>','..\\CM7\\Device\\Components\\OV5640;..\\CM7\\Diagnostics</IncludePath>')
  # Only Cads VariousControls, not linker/assembler settings.
  a=part.index('<Cads>');b=part.index('</Cads>',a);seg=part[a:b]
  seg=seg.replace('<MiscControls>','<MiscControls>--preinclude ../CM7/BSP/bsp_sdram_hal_config.h ',1) if '<MiscControls>' in seg else seg.replace('<VariousControls>','<VariousControls>\n<MiscControls>--preinclude ../CM7/BSP/bsp_sdram_hal_config.h</MiscControls>',1)
  # Add HAL SDRAM/FMC sources only to active CM7 HAL group.
  a_hal=part.index('<GroupName>Drivers/STM32H7xx_HAL_Driver</GroupName>');b_hal=part.index('</Files>',a_hal)
  extra=''
  for f in ['stm32h7xx_hal_sdram.c','stm32h7xx_ll_fmc.c']:
   extra+='            <File><FileName>'+f+'</FileName><FileType>1</FileType><FilePath>../Drivers/STM32H7xx_HAL_Driver/Src/'+f+'</FilePath></File>\n'
  part=part[:b_hal]+extra+part[b_hal:]
  # re-find indices since earlier replacements may alter lengths
  a=part.index('<Cads>');b=part.index('</Cads>',a);part=part[:a]+seg+part[b:]
 parts[i]=part
s='<Target>'.join(parts)
__import__('xml.etree.ElementTree',fromlist=['ElementTree']).fromstring(s)
p.write_bytes(s.encode('utf-8'))
