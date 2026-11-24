#include "bsp_camera.h"
#include "framework_init.h"
#include "stm32h7xx_hal.h"
#include <stdbool.h>
#include <stddef.h>

//V1接线：PA15控制U10的1.5V电源，PH13为PWDN，PH15为RESETB
//PB8为I2C1 SCL，PB9为I2C1 SDA，PA8继续输出原有24MHz XCLK
#define CAMERA_SDA      GPIO_PIN_9
//摄像头控制总线的时钟引脚
#define CAMERA_SCL      GPIO_PIN_8
//摄像头复位引脚，电平时序由BSP管理
#define CAMERA_RESET    GPIO_PIN_15
//摄像头掉电控制引脚
#define CAMERA_PWDN     GPIO_PIN_13
//摄像头数字电源使能引脚
#define CAMERA_DVDD_EN  GPIO_PIN_15
//外部参考时钟输出引脚
#define CAMERA_XCLK    GPIO_PIN_8
//摄像头控制总线时钟所用PLL输出频率，单位赫兹
#define CAMERA_PLLQ_HZ 240000000UL
//摄像头I2C定时计算使用的内核频率，单位赫兹
#define CAMERA_I2C_KERNEL_HZ 120000000UL
//120MHz下分频12：SCL低5.5us、高4.5us，数据建立1.4us、保持0.3us
//加入滤波与同步延迟后不超过100kHz，仍需实测摄像头端边沿
#define CAMERA_I2C_TIMING    0xB0D32C36UL
//传感器控制接口的总线地址，按驱动约定传入
#define CAMERA_I2C_ADDRESS   0x78U
//传感器寄存器访问超时，单位毫秒
#define CAMERA_I2C_TIMEOUT_MS 20U

//记录SCCB是否已经具备访问传感器的上电条件
static bool s_camera_ready;
//记录共享端口是否完成一次性配置
static bool s_camera_pins_ready;
//保存最近一次SCCB传输的HAL错误位，已不使用软件逐字节错误编码
static volatile uint32_t s_sccb_error;
//保存Camera独占的I2C1句柄，不与CM4的I2C2共用
static I2C_HandleTypeDef s_camera_i2c;
//记录I2C1是否完成配置，断电后必须重新初始化
static bool s_camera_i2c_ready;

//校验共享时钟源并初始化Camera独占的I2C1
static SysResult_t Camera_I2cInit(void);

//记录HAL错误并转换为原有通用返回码
static SysResult_t Camera_I2cResult(HAL_StatusTypeDef status);

//在CM4唤醒前配置共享端口，避免两核同时改写GPIOB配置寄存器
void BSP_Camera_EarlyInit(void)
{
	GPIO_InitTypeDef gpio = {0};

	//防止重复配置共享端口
	if ( s_camera_pins_ready)
		return;

	//开启摄像头引脚时钟
	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOH_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();

	//先预装低电平，再把PA15从调试复用切为输出，避免软件配置时误使能
	HAL_GPIO_WritePin(GPIOA, CAMERA_DVDD_EN, GPIO_PIN_RESET);
	gpio.Pin = CAMERA_DVDD_EN;
	gpio.Mode = GPIO_MODE_OUTPUT_PP;
	gpio.Pull = GPIO_PULLDOWN;
	gpio.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOA, &gpio);

	//保持传感器复位，并用原有PH13保持PWDN有效，等待DVDD稳定
	HAL_GPIO_WritePin(GPIOH, CAMERA_PWDN, GPIO_PIN_SET);
	HAL_GPIO_WritePin(GPIOH, CAMERA_RESET, GPIO_PIN_RESET);
	gpio.Pin = CAMERA_RESET | CAMERA_PWDN;
	gpio.Mode = GPIO_MODE_OUTPUT_PP;
	gpio.Pull = GPIO_NOPULL;
	gpio.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOH, &gpio);

	//在唤醒CM4前一次性配置PB8/PB9，避免与OLED的GPIOB配置并发
	gpio.Pin = CAMERA_SDA | CAMERA_SCL;
	gpio.Mode = GPIO_MODE_AF_OD;
	gpio.Alternate = GPIO_AF4_I2C1;
	HAL_GPIO_WritePin(GPIOB, gpio.Pin, GPIO_PIN_SET);
	HAL_GPIO_Init(GPIOB, &gpio);

	s_camera_pins_ready = true;
}

//使能摄像头1.5V数字电源
void BSP_Camera_PowerEnable(void)
{
	//使能只控制U10，不代表传感器已经初始化或允许访问SCCB
	BSP_Camera_EarlyInit();
	HAL_GPIO_WritePin(GPIOA, CAMERA_DVDD_EN, GPIO_PIN_SET);
}

//关闭摄像头1.5V数字电源并禁止继续访问SCCB
void BSP_Camera_PowerDisable(void)
{
	GPIO_InitTypeDef gpio = {0};

	//调用者必须先停止采集并释放资源；此接口不替代Device或Media停止
	BSP_Camera_EarlyInit();
	HAL_GPIO_WritePin(GPIOH, CAMERA_RESET, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(GPIOH, CAMERA_PWDN, GPIO_PIN_SET);

	//仅停本任务独占的I2C1，不重置GPIOB或共享的I2C时钟选择器
	if (s_camera_i2c_ready)
	{
		(void)HAL_I2C_DeInit(&s_camera_i2c);
		s_camera_i2c_ready = false;
	}

	//先停止向传感器输出XCLK，再关闭DVDD；不改PLL或MCO分频
	gpio.Pin = CAMERA_XCLK;
	gpio.Mode = GPIO_MODE_ANALOG;
	gpio.Pull = GPIO_NOPULL;
	HAL_GPIO_Init(GPIOA, &gpio);
	HAL_GPIO_WritePin(GPIOA, CAMERA_DVDD_EN, GPIO_PIN_RESET);
	s_camera_ready = false;
}

//建立数字电源和24MHz时钟，再按外部DVDD时序释放PWDN和RESETB
SysResult_t BSP_Camera_Init(void)
{
	PLL1_ClocksTypeDef clocks;
	uint32_t start;
	uint32_t i;
	SysResult_t result;

	if ( s_camera_ready)
		return SYS_OK;

	BSP_Camera_EarlyInit();

	//检查PLL1Q频率和时钟输出
	HAL_RCCEx_GetPLL1ClockFreq(&clocks);
	if ( __HAL_RCC_GET_FLAG(RCC_FLAG_PLLRDY) == RESET ||
		(RCC->PLLCFGR & RCC_PLLCFGR_DIVQ1EN) == 0U ||
		clocks.PLL1_Q_Frequency != CAMERA_PLLQ_HZ)
		return SYS_ERR_INVALID_STATE;

	SystemCoreClockUpdate();
	if ( SystemCoreClock < 1000000U)
		return SYS_ERR_INVALID_STATE;

	//不清零已运行的周期计数器，避免干扰其他DWT用户
	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
	start = DWT->CYCCNT;
	for (i = 0U;
		 i < 32U;
		 i++)
		__NOP();
	if ( DWT->CYCCNT == start)
		return SYS_ERR_NOT_SUPPORTED;

	//先准备硬件SCCB，失败时保持PWDN、RESETB和DVDD的安全电平
	result = Camera_I2cInit();
	if (result != SYS_OK)
		return result;

	//沿用10ms等待：预留DVDD上升小于5ms及稳定后至少5ms的要求
	//实际电压上升时间必须在TP2测量，软件延时不作为电源稳定证明
	BSP_Camera_PowerEnable();
	HAL_Delay(10U);
	HAL_GPIO_WritePin(GPIOH, CAMERA_PWDN, GPIO_PIN_RESET);

	//HAL同时配置PA8复用输出，240MHz除以10得到24MHz
	HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_PLL1QCLK, RCC_MCODIV_10);

	//PWDN释放后至少1ms才能释放RESETB，之后保留20ms SCCB等待
	HAL_Delay(1U);
	HAL_GPIO_WritePin(GPIOH, CAMERA_RESET, GPIO_PIN_SET);
	HAL_Delay(20U);
	s_camera_ready = true;
	return SYS_OK;
}

//通过硬件I2C读取一个16位地址寄存器
SysResult_t BSP_Camera_ReadReg(uint16_t reg, uint8_t *value)
{
	uint8_t address[2];
	SysResult_t result;

	//未完成上电或I2C配置时禁止访问，不改变调用者的数据
	if (value == NULL)
		return SYS_ERR_INVALID_ARG;
	if (!s_camera_ready || !s_camera_i2c_ready)
		return SYS_ERR_INVALID_STATE;

	//先发寄存器高低字节，阻塞发送完成后HAL产生STOP
	address[0] = (uint8_t)(reg >> 8);
	address[1] = (uint8_t)reg;
	result = Camera_I2cResult(HAL_I2C_Master_Transmit(&s_camera_i2c,
		CAMERA_I2C_ADDRESS, address, 2U, CAMERA_I2C_TIMEOUT_MS));
	if (result != SYS_OK)
		return result;

	//重新START读取单字节，沿用原有STOP分隔的SCCB读时序
	return Camera_I2cResult(HAL_I2C_Master_Receive(&s_camera_i2c,
		CAMERA_I2C_ADDRESS, value, 1U, CAMERA_I2C_TIMEOUT_MS));
}

//通过硬件I2C写入一个16位地址寄存器
SysResult_t BSP_Camera_WriteReg(uint16_t reg, uint8_t value)
{
	uint8_t packet[3];
	SysResult_t result;

	//不允许在断电或未初始化时提交总线事务
	if (!s_camera_ready || !s_camera_i2c_ready)
		return SYS_ERR_INVALID_STATE;

	//设备地址由HAL发送，数据区依次是寄存器高字节、低字节和数值
	packet[0] = (uint8_t)(reg >> 8);
	packet[1] = (uint8_t)reg;
	packet[2] = value;
	result = Camera_I2cResult(HAL_I2C_Master_Transmit(&s_camera_i2c,
		CAMERA_I2C_ADDRESS, packet, 3U, CAMERA_I2C_TIMEOUT_MS));
	if (result != SYS_OK)
		return result;

	//保留原来的软件复位等待，失败传输不伪装成复位成功
	if (reg == 0x3008U && value == 0x82U)
		HAL_Delay(5U);
	return SYS_OK;
}

//注册板级初始化
FRAMEWORK_INIT_REGISTER(BSP_Camera_Init, INIT_LEVEL_BUS);

//校验现有120MHz内核时钟并配置约100kHz硬件I2C
static SysResult_t Camera_I2cInit(void)
{
	SysResult_t result;

	//I2C1/2/3共用选择器，只读校验，不改CM4 OLED已经使用的时钟源
	if (__HAL_RCC_GET_I2C1_SOURCE() != RCC_I2C123CLKSOURCE_D2PCLK1 ||
		HAL_RCC_GetPCLK1Freq() != CAMERA_I2C_KERNEL_HZ)
		return SYS_ERR_INVALID_STATE;
	if (s_camera_i2c_ready)
		return SYS_OK;

	//仅开启I2C1门控，GPIOB已在CM4唤醒前配置，运行期不再次改端口
	__HAL_RCC_I2C1_CLK_ENABLE();
	s_camera_i2c.Instance = I2C1;
	s_camera_i2c.Init.Timing = CAMERA_I2C_TIMING;
	s_camera_i2c.Init.OwnAddress1 = 0U;
	s_camera_i2c.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
	s_camera_i2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
	s_camera_i2c.Init.OwnAddress2 = 0U;
	s_camera_i2c.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
	s_camera_i2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
	s_camera_i2c.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
	result = Camera_I2cResult(HAL_I2C_Init(&s_camera_i2c));
	if (result != SYS_OK)
		return result;

	//开启模拟滤波、关闭数字滤波，与TIMINGR的配置保持一致
	result = Camera_I2cResult(HAL_I2CEx_ConfigAnalogFilter(&s_camera_i2c,
		I2C_ANALOGFILTER_ENABLE));
	if (result == SYS_OK)
		result = Camera_I2cResult(HAL_I2CEx_ConfigDigitalFilter(&s_camera_i2c, 0U));
	if (result != SYS_OK)
	{
		//撤销半初始化外设，保留首次错误，不强行重试或切换为软件I2C
		(void)HAL_I2C_DeInit(&s_camera_i2c);
		return result;
	}
	s_camera_i2c_ready = true;
	return SYS_OK;
}

//保留NACK、总线错误及超时的实际诊断，不将错误转换为成功
static SysResult_t Camera_I2cResult(HAL_StatusTypeDef status)
{
	//HAL_BUSY不保证更新ErrorCode，不能把上一笔NACK误认成本次错误
	s_sccb_error = (status == HAL_ERROR || status == HAL_TIMEOUT) ?
		HAL_I2C_GetError(&s_camera_i2c) : HAL_I2C_ERROR_NONE;
	if (status == HAL_OK)
		return SYS_OK;
	if (status == HAL_BUSY)
		return SYS_ERR_BUSY;
	if (status == HAL_TIMEOUT || (s_sccb_error & HAL_I2C_ERROR_TIMEOUT) != 0U)
		return SYS_ERR_TIMEOUT;
	return SYS_ERR_IO;
}
