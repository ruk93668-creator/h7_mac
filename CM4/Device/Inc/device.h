#ifndef CM4_DEVICE_H
#define CM4_DEVICE_H

#include "framework_list.h"
#include "board_config.h"
#include "sys_result.h"
/* CM4 device interface declarations. */
typedef struct Device Device_t;

#define DEVICE_EXPORT_SECTION ".device_export"

//导出静态设备对象，由设备注册表自动发现
#define DEVICE_EXPORT(obj)                                  \
    static const DeviceExport_t                             \
    g_device_export_##obj                                   \
    __attribute__((used, section(DEVICE_EXPORT_SECTION),    \
                   aligned(4))) =                           \
    {                                                       \
        &(obj).base                                         \
    }

typedef struct
{
    Device_t *device;
} DeviceExport_t;

typedef struct
{

    //执行具体设备的初始化操作
    SysResult_t (*init)(Device_t *dev);

    //执行具体设备的启动操作
    SysResult_t (*start)(Device_t *dev);

    //执行具体设备的停止操作
    SysResult_t (*stop)(Device_t *dev);
} DeviceLifecycleOps_t;

typedef enum
{
    DEVICE_STATE_UNINIT = 0,
    DEVICE_STATE_READY,
    DEVICE_STATE_RUNNING,
	DEVICE_STATE_BUSY,
    DEVICE_STATE_ERROR
} DeviceState_t;

struct Device
{
    const char * name; 
	
	DeviceState_t state;
	
	FrameworkListNode_t node;
	
	const DeviceLifecycleOps_t *lifecycle;
	
	bool auto_init;
};

//遍历设备导出表，注册所有静态设备
SysResult_t Device_RegisterAll(void);

//初始化已注册且auto_init为true的设备，首次失败即返回
SysResult_t Device_InitAll(void);

//注册未初始化设备，拒绝重复对象或名称
SysResult_t Device_Register(Device_t *dev);

//按名称查找已注册设备，未找到时返回NULL
Device_t *Device_Find(const char *name);

//初始化已注册设备，成功后进入READY状态
SysResult_t Device_Init(Device_t *dev);

//启动READY设备，成功后进入RUNNING状态
SysResult_t Device_Start(Device_t *dev);

//停止RUNNING设备，成功后回到READY状态
SysResult_t Device_Stop(Device_t *dev);
#endif /* CM4_DEVICE_H */
