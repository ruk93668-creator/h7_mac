#include "bsp_camera.h"
#include "framework_init.h"
#include "stm32h7xx_hal.h"
#include <stdbool.h>
#include <stddef.h>

//V0接线：PB8为SDA，PB9为SCL，PH15为RESETB，PA8为XCLK
//供电沿用旧板修订：R31已拆，U10 CE硬件拉高，不用GPIO控制供电
#define CAMERA_SDA      GPIO_PIN_8
#define CAMERA_SCL      GPIO_PIN_9
#define CAMERA_RESET    GPIO_PIN_15
#define CAMERA_PLLQ_HZ 240000000UL

static bool s_camera_ready;
static bool s_camera_pins_ready;
static volatile uint32_t s_sccb_error;

//内部功能函数声明
//用DWT提供软件SCCB微秒延时
static void Camera_DelayUs(uint32_t us);

//设置SDA电平，高电平释放开漏引脚
static void Camera_Sda(uint8_t high);

//设置软件SCCB时钟电平
static void Camera_Scl(uint8_t high);

//产生软件SCCB起始条件
static void Camera_Start(void);

//产生软件SCCB停止条件
static void Camera_Stop(void);

//发送一个字节并检查从设备应答
static bool Camera_WriteByte(uint8_t value);

//读取一个字节并发送NACK
static uint8_t Camera_ReadByte(void);

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

	//保持传感器复位
	HAL_GPIO_WritePin(GPIOH, CAMERA_RESET, GPIO_PIN_RESET);
	gpio.Pin = CAMERA_RESET;
	gpio.Mode = GPIO_MODE_OUTPUT_PP;
	gpio.Pull = GPIO_NOPULL;
	gpio.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOH, &gpio);

	//释放SCCB总线，配置为开漏输出
	gpio.Pin = CAMERA_SDA | CAMERA_SCL;
	gpio.Mode = GPIO_MODE_OUTPUT_OD;
	HAL_GPIO_WritePin(GPIOB, gpio.Pin, GPIO_PIN_SET);
	HAL_GPIO_Init(GPIOB, &gpio);

	s_camera_pins_ready = true;
}

//先保持复位，再建立24MHz时钟，最后释放RESETB
SysResult_t BSP_Camera_Init(void)
{
	PLL1_ClocksTypeDef clocks;
	uint32_t start;
	uint32_t i;

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

	//HAL同时配置PA8复用输出，240MHz除以10得到24MHz
	HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_PLL1QCLK, RCC_MCODIV_10);

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

	HAL_Delay(10U);
	HAL_GPIO_WritePin(GPIOH, CAMERA_RESET, GPIO_PIN_SET);
	HAL_Delay(20U);
	s_camera_ready = true;
	return SYS_OK;
}

//沿用旧V0的STOP后重新START读法，地址为0x78/0x79
SysResult_t BSP_Camera_ReadReg(
	uint16_t reg,
	uint8_t *value
)
{
	uint32_t error = 0U;

	//检查参数和初始化状态
	if ( value == NULL)
		return SYS_ERR_INVALID_ARG;
	if ( !s_camera_ready)
		return SYS_ERR_INVALID_STATE;

	//检查总线是否空闲
	Camera_Sda(1U);
	Camera_Scl(1U);
	Camera_DelayUs(10U);
	if ( HAL_GPIO_ReadPin(GPIOB, CAMERA_SDA) == GPIO_PIN_RESET)
		error |= 0x01U;
	if ( HAL_GPIO_ReadPin(GPIOB, CAMERA_SCL) == GPIO_PIN_RESET)
		error |= 0x02U;
	if ( error != 0U)
	{
		s_sccb_error = error;
		return SYS_ERR_BUSY;
	}

	//发送写地址和16位寄存器地址
	Camera_Start();
	if ( !Camera_WriteByte(0x78U))
	{
		error = 0x04U;
		goto done;
	}
	if ( !Camera_WriteByte((uint8_t)(reg >> 8)))
	{
		error = 0x08U;
		goto done;
	}
	if ( !Camera_WriteByte((uint8_t)reg))
	{
		error = 0x10U;
		goto done;
	}

	//重新开始单字节读取
	Camera_Stop();
	Camera_Start();
	if ( !Camera_WriteByte(0x79U))
	{
		error = 0x20U;
		goto done;
	}
	*value = Camera_ReadByte();
done:
	//成功或失败都释放总线
	Camera_Stop();
	s_sccb_error = error;
	return error == 0U ? SYS_OK : SYS_ERR_IO;
}

//发送写地址、寄存器地址和数据，每个字节检查ACK
SysResult_t BSP_Camera_WriteReg(
	uint16_t reg,
	uint8_t value
)
{
	uint32_t error = 0U;

	if ( !s_camera_ready)
		return SYS_ERR_INVALID_STATE;

	//总线忙时不开始事务
	Camera_Sda(1U);
	Camera_Scl(1U);
	Camera_DelayUs(10U);
	if ( HAL_GPIO_ReadPin(GPIOB, CAMERA_SDA) == GPIO_PIN_RESET)
		error |= 0x01U;
	if ( HAL_GPIO_ReadPin(GPIOB, CAMERA_SCL) == GPIO_PIN_RESET)
		error |= 0x02U;
	if ( error != 0U)
	{
		s_sccb_error = error;
		return SYS_ERR_BUSY;
	}

	Camera_Start();
	if ( !Camera_WriteByte(0x78U))
	{
		error = 0x04U;
		goto done;
	}
	if ( !Camera_WriteByte((uint8_t)(reg >> 8)))
	{
		error = 0x08U;
		goto done;
	}
	if ( !Camera_WriteByte((uint8_t)reg))
	{
		error = 0x10U;
		goto done;
	}
	if ( !Camera_WriteByte(value))
		error = 0x40U;
done:
	//失败也释放总线，软件复位后等待寄存器恢复
	Camera_Stop();
	s_sccb_error = error;
	if ( error != 0U)
		return SYS_ERR_IO;
	if ( reg == 0x3008U && value == 0x82U)
		HAL_Delay(5U);
	return SYS_OK;
}

//注册板级初始化
FRAMEWORK_INIT_REGISTER(
	BSP_Camera_Init,
	INIT_LEVEL_BUS
);

//内部功能函数实现
//用DWT提供软件SCCB位时序，不改变系统Tick
static void Camera_DelayUs(uint32_t us)
{
	uint32_t start = DWT->CYCCNT;
	uint32_t cycles = (SystemCoreClock / 1000000U) * us;

	while ((uint32_t)(DWT->CYCCNT - start) < cycles)
	{
	}
}

//设置SDA电平，开漏高电平表示释放引脚
static void Camera_Sda(uint8_t high)
{
	HAL_GPIO_WritePin(GPIOB, CAMERA_SDA, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

//设置SCL电平，产生软件时钟
static void Camera_Scl(uint8_t high)
{
	HAL_GPIO_WritePin(GPIOB, CAMERA_SCL, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

//SCL为高时拉低SDA，产生起始条件
static void Camera_Start(void)
{
	Camera_Sda(1U);
	Camera_Scl(1U);
	Camera_DelayUs(5U);
	Camera_Sda(0U);
	Camera_DelayUs(5U);
	Camera_Scl(0U);
}

//SCL为高时释放SDA，结束当前传输
static void Camera_Stop(void)
{
	Camera_Sda(0U);
	Camera_DelayUs(5U);
	Camera_Scl(1U);
	Camera_DelayUs(5U);
	Camera_Sda(1U);
	Camera_DelayUs(5U);
}

//高位先发，释放SDA后采样从设备ACK
static bool Camera_WriteByte(uint8_t value)
{
	uint32_t bit;
	bool ack;

	//逐位发送数据
	for (bit = 0U;
		 bit < 8U;
		 bit++)
	{
		Camera_Sda((value & 0x80U) != 0U);
		Camera_DelayUs(5U);
		Camera_Scl(1U);
		Camera_DelayUs(5U);
		Camera_Scl(0U);
		value <<= 1;
	}

	//释放SDA，读取从设备应答
	Camera_Sda(1U);
	Camera_DelayUs(5U);
	Camera_Scl(1U);
	Camera_DelayUs(5U);
	ack = HAL_GPIO_ReadPin(GPIOB, CAMERA_SDA) == GPIO_PIN_RESET;
	Camera_Scl(0U);
	return ack;
}

//读取单字节后发送NACK，不继续读取下一寄存器
static uint8_t Camera_ReadByte(void)
{
	uint32_t bit;
	uint8_t value = 0U;

	Camera_Sda(1U);
	for (bit = 0U;
		 bit < 8U;
		 bit++)
	{
		Camera_DelayUs(5U);
		Camera_Scl(1U);
		Camera_DelayUs(5U);
		value = (uint8_t)((value << 1) |
				(HAL_GPIO_ReadPin(GPIOB, CAMERA_SDA) == GPIO_PIN_SET ? 1U : 0U));
		Camera_Scl(0U);
	}
	Camera_Sda(1U);
	Camera_DelayUs(5U);
	Camera_Scl(1U);
	Camera_DelayUs(5U);
	Camera_Scl(0U);
	return value;
}
