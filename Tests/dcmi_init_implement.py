from pathlib import Path
import hashlib,json,shutil,re,xml.etree.ElementTree as E
r=Path('.');t=r/'Tests/DcmiInit';t.mkdir(exist_ok=True)
paths=[p for d in ['CM7','CM4','Common','Board'] for p in (r/d).rglob('*') if p.is_file()]+list((r/'MDK-ARM').glob('*.sct'))+[r/'MDK-ARM/h7_mac.uvprojx']
(t/'baseline.json').write_text(json.dumps({str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},indent=2),encoding='utf-8')
for f in ['CM7/Core/Src/main.c','CM7/BSP/bsp_dcmi.c','CM7/BSP/bsp_dcmi.h','MDK-ARM/h7_mac.uvprojx']:
 dst=t/'before'/f;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(f,dst)
shutil.copyfile('D:/codex_proe/h7_codex/h7_mac/CM7/Core/Src/main.c',t/'v0_reference_main.c')
def write(f,s):Path(f).write_bytes(s.replace('\n','\r\n').encode('gbk'))
write('CM7/BSP/bsp_dcmi.h','''#ifndef CM7_BSP_DCMI_H
#define CM7_BSP_DCMI_H

#include "sys_result.h"

//唤醒CM4前预配置引脚，避免GPIOB初始化冲突
void BSP_DCMI_EarlyInit(void);

//仅初始化DCMI和DMA，不启动采集
SysResult_t BSP_DCMI_Init(void);

#endif
''')
write('CM7/BSP/bsp_dcmi_hal_config.h','''#ifndef CM7_BSP_DCMI_HAL_CONFIG_H
#define CM7_BSP_DCMI_HAL_CONFIG_H

//由CM7编译器强制包含，避免CubeMX覆盖模块选择
#ifndef HAL_DCMI_MODULE_ENABLED
#define HAL_DCMI_MODULE_ENABLED
#endif

#endif
''')
write('CM7/BSP/bsp_dcmi.c','''#include "bsp_dcmi.h"
#include "framework_init.h"
#include "stm32h7xx_hal.h"
#include <stdbool.h>

//沿用V0测试工程优先级，CM7 SysTick优先级为15
#define DCMI_IRQ_PRIORITY 5U
#define DCMI_DMA_IRQ_PRIORITY 5U

static DCMI_HandleTypeDef s_dcmi;
static DMA_HandleTypeDef s_dcmi_dma;
static bool s_dcmi_pins_ready;
static bool s_dcmi_ready;

static void Dcmi_ConfigPin(
	GPIO_TypeDef *port,
	uint32_t pins
)
{
	GPIO_InitTypeDef gpio = {0};

	gpio.Pin = pins;
	gpio.Mode = GPIO_MODE_AF_PP;
	gpio.Pull = GPIO_NOPULL;
	gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
	gpio.Alternate = GPIO_AF13_DCMI;
	HAL_GPIO_Init(port, &gpio);
}

void BSP_DCMI_EarlyInit(void)
{
	if ( s_dcmi_pins_ready)
		return;

	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();
	__HAL_RCC_GPIOC_CLK_ENABLE();
	__HAL_RCC_GPIOE_CLK_ENABLE();
	__HAL_RCC_GPIOG_CLK_ENABLE();
	__HAL_RCC_GPIOH_CLK_ENABLE();

	//V0 D0、D1
	Dcmi_ConfigPin(GPIOC, GPIO_PIN_6 | GPIO_PIN_7);
	//V0 D2、D3
	Dcmi_ConfigPin(GPIOG, GPIO_PIN_10);
	Dcmi_ConfigPin(GPIOH, GPIO_PIN_12);
	//V0 D4、D6、D7
	Dcmi_ConfigPin(GPIOE, GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6);
	//V0 D5、VSYNC，提前配置以避开CM4的PB10/PB11初始化
	Dcmi_ConfigPin(GPIOB, GPIO_PIN_6 | GPIO_PIN_7);
	//V0 HSYNC、PCLK
	Dcmi_ConfigPin(GPIOA, GPIO_PIN_4 | GPIO_PIN_6);

	s_dcmi_pins_ready = true;
}

//HAL初始化时保证GPIO和外设时钟就绪
void HAL_DCMI_MspInit(DCMI_HandleTypeDef *hdcmi)
{
	if ( hdcmi->Instance != DCMI)
		return;

	BSP_DCMI_EarlyInit();
	__HAL_RCC_DCMI_CLK_ENABLE();
}

static SysResult_t Dcmi_HalResult(HAL_StatusTypeDef status)
{
	if ( status == HAL_OK)
		return SYS_OK;
	if ( status == HAL_BUSY)
		return SYS_ERR_BUSY;
	if ( status == HAL_TIMEOUT)
		return SYS_ERR_TIMEOUT;
	return SYS_ERR_IO;
}

SysResult_t BSP_DCMI_Init(void)
{
	HAL_StatusTypeDef status;

	if ( s_dcmi_ready)
		return SYS_OK;

	BSP_DCMI_EarlyInit();
	__HAL_RCC_DMA1_CLK_ENABLE();
	__HAL_RCC_DCMI_CLK_ENABLE();

	//初始化不接管正在运行的采集或DMA
	if ( (DMA1_Stream0->CR & DMA_SxCR_EN) != 0U ||
		(DCMI->CR & (DCMI_CR_ENABLE | DCMI_CR_CAPTURE)) != 0U)
		return SYS_ERR_BUSY;

	HAL_NVIC_DisableIRQ(DMA1_Stream0_IRQn);
	HAL_NVIC_DisableIRQ(DCMI_IRQn);

	//外设到内存，32位传输，普通模式，不启动Stream
	s_dcmi_dma.Instance = DMA1_Stream0;
	s_dcmi_dma.Init.Request = DMA_REQUEST_DCMI;
	s_dcmi_dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
	s_dcmi_dma.Init.PeriphInc = DMA_PINC_DISABLE;
	s_dcmi_dma.Init.MemInc = DMA_MINC_ENABLE;
	s_dcmi_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
	s_dcmi_dma.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
	s_dcmi_dma.Init.Mode = DMA_NORMAL;
	s_dcmi_dma.Init.Priority = DMA_PRIORITY_HIGH;
	s_dcmi_dma.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
	s_dcmi_dma.Init.MemBurst = DMA_MBURST_SINGLE;
	s_dcmi_dma.Init.PeriphBurst = DMA_PBURST_SINGLE;
	status = HAL_DMA_Init(&s_dcmi_dma);
	if ( status != HAL_OK)
		return Dcmi_HalResult(status);

	//8位DVP、硬件同步，PCLK上升沿采样
	s_dcmi.Instance = DCMI;
	s_dcmi.Init.SynchroMode = DCMI_SYNCHRO_HARDWARE;
	s_dcmi.Init.PCKPolarity = DCMI_PCKPOLARITY_RISING;
	s_dcmi.Init.VSPolarity = DCMI_VSPOLARITY_HIGH;
	s_dcmi.Init.HSPolarity = DCMI_HSPOLARITY_HIGH;
	s_dcmi.Init.CaptureRate = DCMI_CR_ALL_FRAME;
	s_dcmi.Init.ExtendedDataMode = DCMI_EXTEND_DATA_8B;
	s_dcmi.Init.JPEGMode = DCMI_JPEG_DISABLE;
	s_dcmi.Init.ByteSelectMode = DCMI_BSM_ALL;
	s_dcmi.Init.ByteSelectStart = DCMI_OEBS_ODD;
	s_dcmi.Init.LineSelectMode = DCMI_LSM_ALL;
	s_dcmi.Init.LineSelectStart = DCMI_OELS_ODD;

	//同时建立DCMI到DMA、DMA到Parent的关联
	__HAL_LINKDMA(&s_dcmi, DMA_Handle, s_dcmi_dma);
	status = HAL_DCMI_Init(&s_dcmi);
	if ( status != HAL_OK)
	{
		//失败不开放NVIC，释放本次DMA初始化
		if ( HAL_DMA_DeInit(&s_dcmi_dma) != HAL_OK)
			return SYS_ERR_IO;
		return Dcmi_HalResult(status);
	}

	//清除旧标志和挂起状态，再开放NVIC
	__HAL_DCMI_CLEAR_FLAG(&s_dcmi, DCMI_FLAG_FRAMERI | DCMI_FLAG_OVRRI |
		DCMI_FLAG_ERRRI | DCMI_FLAG_VSYNCRI | DCMI_FLAG_LINERI);
	HAL_NVIC_ClearPendingIRQ(DMA1_Stream0_IRQn);
	HAL_NVIC_ClearPendingIRQ(DCMI_IRQn);
	HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, DCMI_DMA_IRQ_PRIORITY, 0U);
	HAL_NVIC_SetPriority(DCMI_IRQn, DCMI_IRQ_PRIORITY, 0U);
	HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
	HAL_NVIC_EnableIRQ(DCMI_IRQn);

	s_dcmi_ready = true;
	return SYS_OK;
}

void DMA1_Stream0_IRQHandler(void)
{
	HAL_DMA_IRQHandler(&s_dcmi_dma);
}

void DCMI_IRQHandler(void)
{
	HAL_DCMI_IRQHandler(&s_dcmi);
}

//系统时钟完成后初始化底层，不依赖帧缓冲
FRAMEWORK_INIT_REGISTER(
	BSP_DCMI_Init,
	INIT_LEVEL_BUS
);
''')
p=r/'CM7/Core/Src/main.c';s=p.read_bytes().decode('gbk');s=s.replace('#include "bsp_camera.h"','#include "bsp_camera.h"\r\n#include "bsp_dcmi.h"')
s=s.replace('BSP_Camera_EarlyInit();','BSP_DCMI_EarlyInit();\r\nBSP_Camera_EarlyInit();');p.write_bytes(s.encode('gbk'))
p=r/'MDK-ARM/h7_mac.uvprojx';s=p.read_bytes().decode('utf-8');parts=s.split('<Target>')
for i in range(1,len(parts)):
 part=parts[i]
 # header shown in mirrored CM7 BSP group, already excluded from CM4
 pattern=r'(<File>\s*<FileName>bsp_dcmi.h</FileName>.*?<FilePath>../CM7/BSP/bsp_dcmi.h</FilePath>\s*</File>)'
 block='\n            <File><FileName>bsp_dcmi_hal_config.h</FileName><FileType>5</FileType><FilePath>../CM7/BSP/bsp_dcmi_hal_config.h</FilePath></File>'
 part,n=re.subn(pattern,lambda m:m.group(1)+block,part,flags=re.S);assert n==1
 if '<TargetName>h7_mac_CM7</TargetName>' in part:
  part=part.replace('--preinclude ../CM7/BSP/bsp_sdram_hal_config.h','--preinclude ../CM7/BSP/bsp_sdram_hal_config.h --preinclude ../CM7/BSP/bsp_dcmi_hal_config.h')
  a=part.index('<GroupName>Drivers/STM32H7xx_HAL_Driver</GroupName>');b=part.index('</Files>',a)
  block='            <File><FileName>stm32h7xx_hal_dcmi.c</FileName><FileType>1</FileType><FilePath>../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dcmi.c</FilePath></File>\n'
  part=part[:b]+block+part[b:]
 parts[i]=part
s='<Target>'.join(parts);E.fromstring(s);p.write_bytes(s.encode('utf-8'))
print('DCMI BSP implemented; CM7 only HAL DCMI enabled; early pin configuration added inside existing USER boot area.')
