//CM4 OLED设备驱动：初始化SSD1306、维护帧缓冲并按页异步刷新
//初始化同步发送命令，运行期由refresh启动、process轮询推进
#include "oled_device.h"
#include "bsp_iic.h"
#include <string.h>

//SSD1306每页包含8行像素
#define OLED_PAGE_HEIGHT  8U

/* SSD1306 I2C 控制字节与显示初始化命令 */
#define SSD1306_CONTROL_COMMAND       0x00U  /* 后续字节作为命令传输 */
#define SSD1306_CMD_DISPLAY_OFF       0xAEU  /* 初始化前关闭显示 */
#define SSD1306_CMD_DISPLAY_CLOCK     0xD5U  /* 设置显示时钟分频及振荡频率，后接配置参数 */
#define SSD1306_CMD_MULTIPLEX_RATIO   0xA8U  /* 设置扫描行数，128x32 面板后接 0x1F（32 行） */
#define SSD1306_CMD_DISPLAY_OFFSET    0xD3U  /* 设置显示垂直偏移，后接偏移量 */
#define SSD1306_CMD_MEMORY_ADDRESSING 0x20U  /* 设置显存寻址模式，后接 0x02 选择 Page 模式 */
#define SSD1306_CMD_SEGMENT_REMAP_OFF 0xA0U  /* 列地址 0 映射至 SEG0，选择一种左右映射方向 */
#define SSD1306_CMD_SEGMENT_REMAP_ON  0xA1U  /* 列地址 127 映射至 SEG0，反转左右映射方向 */
#define SSD1306_CMD_COM_SCAN_NORMAL   0xC0U  /* COM 正向扫描，选择一种上下扫描方向 */
#define SSD1306_CMD_COM_SCAN_REMAP    0xC8U  /* COM 反向扫描，反转上下扫描方向 */
#define SSD1306_CMD_COM_PINS          0xDAU  /* 配置 COM 引脚连接方式，后接匹配 128x32 面板的参数 */
#define SSD1306_CMD_CHARGE_PUMP       0x8DU  /* 配置内部升压电路，后接升压配置参数 */
#define SSD1306_CMD_DISPLAY_ON        0xAFU  /* 初始化结束后开启显示 */
#define SSD1306_CMD_DISPLAY_START_LINE  0x40U  /* 设置显示起始行，低 6 位为行号；0x40 表示从第 0 行开始 */
#define SSD1306_CMD_SET_CONTRAST        0x81U  /* 设置显示对比度，后接对比度参数 */
#define SSD1306_CMD_PRECHARGE           0xD9U  /* 设置预充电周期，后接阶段 1、阶段 2 的周期参数 */
#define SSD1306_CMD_VCOMH               0xDBU  /* 设置 VCOMH 取消选择电平，后接电平配置参数 */
#define SSD1306_CMD_DEACTIVATE_SCROLL   0x2EU  /* 停止硬件滚动 */
#define SSD1306_CMD_DISPLAY_RAM         0xA4U  /* 按显存内容显示，退出全屏点亮模式 */
#define SSD1306_CMD_NORMAL_DISPLAY      0xA6U  /* 正常显示：显存位 1 点亮，位 0 熄灭 */
#define SSD1306_CMD_ENTIRE_DISPLAY_ON  0xA5U//测试命令
#define OLED_DEFAULT_I2C_ADDR  0x3CU

//内部操作函数
static SysResult_t Oled_Init(Device_t *dev);

static SysResult_t Oled_ClearImpl(
    OledDevice_t *oled);

static SysResult_t Oled_DrawPixelImpl(
    OledDevice_t *oled,
    uint16_t x,
    uint16_t y,
    bool on);

static SysResult_t Oled_RefreshImpl(
    OledDevice_t *oled);

static SysResult_t Oled_ProcessImpl(OledDevice_t *oled);

static SysResult_t Oled_WriteCommand(
    OledDevice_t *oled,
    uint8_t command
);

//通用生命周期回调，当前只提供初始化
static const DeviceLifecycleOps_t s_oled_lifecycle =
{
	.init  = Oled_Init,
	.start = NULL,
	.stop  = NULL
};
//OLED操作表，绑定绘图、启动刷新和轮询处理函数
static const OledOps_t s_oled_ops =
{
    .clear = Oled_ClearImpl,
	.draw_pixel = Oled_DrawPixelImpl,
	.refresh = Oled_RefreshImpl,
	.process = Oled_ProcessImpl
};

//静态OLED设备实例，按板级配置决定是否自动初始化
static OledDevice_t s_oled =
{
    .base = {
        .name = "oled",
        .state = DEVICE_STATE_UNINIT,
        .lifecycle = &s_oled_lifecycle,
		.auto_init = BOARD_OLED_AUTO_INIT
    },

    .ops = &s_oled_ops,
    .i2c_addr = OLED_DEFAULT_I2C_ADDR
};

//导出设备对象，由Device Model自动注册
#if BOARD_HAS_OLED
DEVICE_EXPORT(s_oled);
#endif

//初始化SSD1306，任意命令或参数发送失败时立即返回错误
static SysResult_t Oled_Init(Device_t *dev)
{
	OledDevice_t *oled;
	SysResult_t result;

	if (dev == NULL)
		return SYS_ERR_INVALID_ARG;

	//由通用设备成员取得完整OLED对象
	oled = FRAMEWORK_CONTAINER_OF(dev, OledDevice_t, base);

	//1. 关闭显示，进入配置阶段
	result = Oled_WriteCommand(oled, SSD1306_CMD_DISPLAY_OFF);
	if (result != SYS_OK) return result;

	//2. 显示时钟：分频为 1，默认振荡频率
	result = Oled_WriteCommand(oled, SSD1306_CMD_DISPLAY_CLOCK);
	if (result != SYS_OK) return result;
	result = Oled_WriteCommand(oled, 0x80U);
	if (result != SYS_OK) return result;

	//3. 配置 32 行扫描
	result = Oled_WriteCommand(oled, SSD1306_CMD_MULTIPLEX_RATIO);
	if (result != SYS_OK) return result;
	result = Oled_WriteCommand(oled, 0x1FU);
	if (result != SYS_OK) return result;

	//4. 显示偏移为 0
	result = Oled_WriteCommand(oled, SSD1306_CMD_DISPLAY_OFFSET);
	if (result != SYS_OK) return result;
	result = Oled_WriteCommand(oled, 0x00U);
	if (result != SYS_OK) return result;

	//5. 从第 0 行开始显示
	result = Oled_WriteCommand(oled, SSD1306_CMD_DISPLAY_START_LINE);
	if (result != SYS_OK) return result;

	//6. 启用内部电荷泵
	result = Oled_WriteCommand(oled, SSD1306_CMD_CHARGE_PUMP);
	if (result != SYS_OK) return result;
	result = Oled_WriteCommand(oled, 0x14U);
	if (result != SYS_OK) return result;

	//7. 选择 Page 寻址模式
	result = Oled_WriteCommand(oled, SSD1306_CMD_MEMORY_ADDRESSING);
	if (result != SYS_OK) return result;
	result = Oled_WriteCommand(oled, 0x02U);
	if (result != SYS_OK) return result;

	//8. 反转 SEG 水平映射
	result = Oled_WriteCommand(oled, SSD1306_CMD_SEGMENT_REMAP_ON);
	if (result != SYS_OK) return result;

	//9. 反转 COM 垂直扫描方向
	result = Oled_WriteCommand(oled, SSD1306_CMD_COM_SCAN_REMAP);
	if (result != SYS_OK) return result;

	//10. 128x32 面板：Sequential COM 引脚配置
	result = Oled_WriteCommand(oled, SSD1306_CMD_COM_PINS);
	if (result != SYS_OK) return result;
	result = Oled_WriteCommand(oled, 0x02U);
	if (result != SYS_OK) return result;

	//11. 设置显示对比度
	result = Oled_WriteCommand(oled, SSD1306_CMD_SET_CONTRAST);
	if (result != SYS_OK) return result;
	result = Oled_WriteCommand(oled, 0x8FU);
	if (result != SYS_OK) return result;

	//12. 设置预充电周期
	result = Oled_WriteCommand(oled, SSD1306_CMD_PRECHARGE);
	if (result != SYS_OK) return result;
	result = Oled_WriteCommand(oled, 0xF1U);
	if (result != SYS_OK) return result;

	//13. 设置 VCOMH 电压等级
	result = Oled_WriteCommand(oled, SSD1306_CMD_VCOMH);
	if (result != SYS_OK) return result;
	result = Oled_WriteCommand(oled, 0x30U);
	if (result != SYS_OK) return result;

	//14. 关闭硬件滚动
	result = Oled_WriteCommand(oled, SSD1306_CMD_DEACTIVATE_SCROLL);
	if (result != SYS_OK) return result;

	//15. 按 GDDRAM 内容显示
	result = Oled_WriteCommand(oled, SSD1306_CMD_DISPLAY_RAM);
	if (result != SYS_OK) return result;

	//16. 正常显示，不反色
	result = Oled_WriteCommand(oled, SSD1306_CMD_NORMAL_DISPLAY);
	if (result != SYS_OK) return result;

	//17. 配置完成，开启显示
	result = Oled_WriteCommand(oled, SSD1306_CMD_DISPLAY_ON);
	if (result != SYS_OK) return result;


//	//18. 临时测试：强制点亮全部像素
//	result = Oled_WriteCommand(oled, SSD1306_CMD_ENTIRE_DISPLAY_ON);
//	if (result != SYS_OK)
//		return result;

	return SYS_OK;
}

//清空帧缓冲，调用refresh后才会更新屏幕
static SysResult_t Oled_ClearImpl(
    OledDevice_t *oled)
{
	if ( oled == NULL)
		return SYS_ERR_INVALID_ARG;
	
	//所有像素位清零，保留刷新状态
	memset(oled->framebuffer, 0, sizeof(oled->framebuffer));
	
    return SYS_OK;
}

//修改帧缓冲中的一个像素，不直接发送IIC数据
static SysResult_t Oled_DrawPixelImpl(
    OledDevice_t *oled,
    uint16_t x,
    uint16_t y,
    bool on)
{
	uint16_t index;  //像素所在的帧缓冲字节下标  
	uint8_t bit;     //像素在该字节中的位位置    
	uint8_t mask;    //用于置位或清零的像素掩码
	
	//检查设备指针和像素坐标，防止访问越界
	if ( oled == NULL)
		return SYS_ERR_INVALID_ARG;
	if ( y >= OLED_HEIGHT ||
		 x >= OLED_WIDTH)
		return SYS_ERR_INVALID_ARG;
	
	//根据页号和列号计算帧缓冲下标
	index =  ( y / OLED_PAGE_HEIGHT ) * OLED_WIDTH + x;
	
	//取页内行号，生成对应的像素位掩码
	bit = y % OLED_PAGE_HEIGHT;
	mask = (uint8_t)( 1U << bit);
	
	//只修改一个像素
	if ( on == true)
		oled->framebuffer[index] |= mask;
	else
		oled->framebuffer[index] &= (uint8_t)(~mask);
	
	return SYS_OK;
}

//按页刷新使用的数据控制字节和定位命令
#define SSD1306_CONTROL_DATA       0x40U
#define SSD1306_CMD_PAGE_BASE      0xB0U
#define SSD1306_CMD_COLUMN_LOW     0x00U
#define SSD1306_CMD_COLUMN_HIGH    0x10U

//启动整屏刷新，先发送第0页的页号和列地址
static SysResult_t Oled_RefreshImpl(
    OledDevice_t *oled)
{
	SysResult_t result ;
	uint8_t cmd[4] =
	{
		SSD1306_CONTROL_COMMAND,
		SSD1306_CMD_PAGE_BASE,
		SSD1306_CMD_COLUMN_LOW,
		SSD1306_CMD_COLUMN_HIGH
	};

	if ( oled == NULL)
		return SYS_ERR_INVALID_ARG;

	//上一轮刷新未结束，拒绝重复启动
	if (oled->refresh_state != OLED_REFRESH_IDLE)
		return SYS_ERR_BUSY;
	
	//异步提交第一页定位命令，后续由process处理完成结果
	result = BSP_IIC_WriteAsync(
	oled->i2c_addr,
	cmd,
	sizeof(cmd));	
	
	if (result != SYS_OK)
		return result;
	
	//命令提交成功后记录刷新进度
	oled->refresh_page = 0U;
	oled->refresh_state = OLED_REFRESH_WAIT_CMD;
	
	return SYS_OK;
}

//轮询IIC完成结果，依次推进每页的命令和数据传输
//返回SYS_OK也可能仍在等待，整屏完成时refresh_state恢复IDLE
static SysResult_t Oled_ProcessImpl(OledDevice_t *oled)
{
    SysResult_t result;
	uint8_t cmd[4] ={
    SSD1306_CONTROL_COMMAND,
    SSD1306_CMD_PAGE_BASE,
    SSD1306_CMD_COLUMN_LOW,
    SSD1306_CMD_COLUMN_HIGH
	};
	//1字节数据控制字节加当前页128字节像素数据
	uint8_t tx_buffer[OLED_WIDTH + 1U];
    if (oled == NULL)
        return SYS_ERR_INVALID_ARG;

    switch (oled->refresh_state)
    {
        case OLED_REFRESH_IDLE:
        {
            //没有刷新任务，本轮直接结束
            return SYS_OK;
        }

        case OLED_REFRESH_WAIT_CMD:
        {
			//等待定位命令结果，未完成时留到下次轮询
			if (!BSP_IIC_TakeResult(&result))
				return SYS_OK;

			if (result != SYS_OK)
			{
				//结束本轮刷新并上报错误
				oled->refresh_state = OLED_REFRESH_IDLE;
				return result;
			}

			//组装当前页数据，首字节标记后续内容为像素数据
			tx_buffer[0] = SSD1306_CONTROL_DATA;

			memcpy(
				&tx_buffer[1],
				&oled->framebuffer[oled->refresh_page * OLED_WIDTH],
				OLED_WIDTH
			);

			result = BSP_IIC_WriteAsync(
				oled->i2c_addr,
				tx_buffer,
				sizeof(tx_buffer)
			);

			if (result != SYS_OK)
			{
				//结束本轮刷新并上报错误
				oled->refresh_state = OLED_REFRESH_IDLE;
				return result;
			}

			//数据提交成功，转入当前页数据等待阶段
			oled->refresh_state = OLED_REFRESH_WAIT_DATA;

            return SYS_OK;
        }

        case OLED_REFRESH_WAIT_DATA:
        {
			//等待当前页像素数据传输结果
			if (!BSP_IIC_TakeResult(&result))
				return SYS_OK;

			if (result != SYS_OK)
			{
				//结束本轮刷新并上报错误
				oled->refresh_state = OLED_REFRESH_IDLE;
				return result;
			}
			
			//最后一页发送完成，结束本轮整屏刷新
			if (oled->refresh_page == OLED_HEIGHT/OLED_PAGE_HEIGHT - 1u)
			{
				oled->refresh_state = OLED_REFRESH_IDLE;
				return result;
			}				
			
			//推进到下一页，并重新发送页号和列地址
			oled->refresh_page++;
			
			cmd[1] = (uint8_t)(SSD1306_CMD_PAGE_BASE | oled->refresh_page);
			
			result = BSP_IIC_WriteAsync(
			oled->i2c_addr,
			cmd,
			sizeof(cmd));	
			
			if (result != SYS_OK)
			{
				//结束本轮刷新并上报错误
				oled->refresh_state = OLED_REFRESH_IDLE;
				return result;
			}
			
			//下一页命令提交成功，继续等待命令结果
			oled->refresh_state = OLED_REFRESH_WAIT_CMD;
			return SYS_OK;
        }

        default:
            //拒绝未定义的刷新状态
            return SYS_ERR_INVALID_STATE;
    }
}

//同步发送一个命令或参数，首字节0x00标记命令传输
static SysResult_t Oled_WriteCommand(
    OledDevice_t *oled,
    uint8_t command
)
{
	if ( oled == NULL)
		return SYS_ERR_INVALID_ARG;
	
	uint8_t cmd[2] = {
	SSD1306_CONTROL_COMMAND,
	command
	};
	
	return BSP_IIC_WriteSync(oled->i2c_addr,
					  cmd, sizeof(cmd));
}
