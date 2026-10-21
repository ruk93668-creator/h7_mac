//CM4 OLED设备接口：绘图修改帧缓冲，refresh启动刷新，process轮询推进
#ifndef CM4_OLED_DEVICE_H
#define CM4_OLED_DEVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "device.h"

//显示尺寸，单色帧缓冲每8个像素占1字节
#define OLED_WIDTH          128U
#define OLED_HEIGHT         32U
#define OLED_BUFFER_SIZE    (OLED_WIDTH * OLED_HEIGHT / 8U)


typedef struct OledDevice OledDevice_t;

//OLED专有操作，通过设备对象的ops调用
typedef struct
{
    //清空内存中的帧缓冲，不立即更新屏幕
    SysResult_t (*clear)(OledDevice_t *oled);

    //修改指定像素，on为true点亮，为false熄灭
    SysResult_t (*draw_pixel)(
        OledDevice_t *oled,
        uint16_t x,
        uint16_t y,
        bool on
    );

    //启动整屏异步刷新，返回成功表示已提交第一页命令
    SysResult_t (*refresh)(OledDevice_t *oled);
		
	//轮询IIC结果并推进刷新，刷新期间需要持续调用
	SysResult_t (*process)(OledDevice_t *oled);

} OledOps_t;

//按页刷新的状态，区分命令传输和像素数据传输
typedef enum
{
    OLED_REFRESH_IDLE = 0,   //当前没有刷新任务
    OLED_REFRESH_WAIT_CMD,   //等待当前页定位命令传输完成
    OLED_REFRESH_WAIT_DATA   //等待当前页128字节像素数据传输完成
} OledRefreshState_t;

//OLED设备对象，包含通用设备信息、刷新进度和帧缓冲
struct OledDevice
{
    Device_t base;          //通用设备成员，用于注册和生命周期管理

    const OledOps_t *ops;    //OLED绘图和刷新操作表

    uint8_t i2c_addr;        //7位IIC地址，发送时由BSP转换为HAL地址格式

	uint8_t refresh_page;    //当前刷新页，128x32面板共4页
	OledRefreshState_t refresh_state; //当前异步刷新阶段
	
    //按页保存像素，每字节对应同一列的8个垂直像素
    uint8_t framebuffer[OLED_BUFFER_SIZE];
};




#endif /* CM4_OLED_DEVICE_H */
