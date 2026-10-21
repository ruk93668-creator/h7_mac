#include "device.h"
#include "framework_init.h"
#include <string.h>

static FrameworkListNode_t s_device_head = {
    &s_device_head,
    &s_device_head
};

/* 自动遍历链接段中的设备声明 */
extern const DeviceExport_t Image$$ER_DEVICE_EXPORT$$Base[];
extern const DeviceExport_t Image$$ER_DEVICE_EXPORT$$Limit[];

SysResult_t Device_RegisterAll(void)
{
	const DeviceExport_t *entry;
	const DeviceExport_t *end;
	SysResult_t error = SYS_OK;
	
	entry = Image$$ER_DEVICE_EXPORT$$Base;
	end = Image$$ER_DEVICE_EXPORT$$Limit;

	while(entry < end)
	{
		if ( entry->device == NULL)
			return SYS_ERR_INVALID_ARG;
		
		error = Device_Register(entry->device);
		
		if ( error != SYS_OK)
			return error;
		
		entry++;
	}
	
	return SYS_OK;
}

FRAMEWORK_INIT_REGISTER(
    Device_RegisterAll,
    INIT_LEVEL_BUS
);

/* 遍历 Registry，初始化符合策略的设备 */
SysResult_t Device_InitAll(void)
{
	FrameworkListNode_t *pos;
	Device_t *dev;
	SysResult_t error = SYS_OK;
	
	for (pos = s_device_head.next;
		 pos != &s_device_head;
		 pos = pos->next)
	{
		dev = FRAMEWORK_CONTAINER_OF(
			  pos, Device_t, node);
		
		if ( dev->auto_init == false)
			continue;
		
		error = Device_Init(dev);
		
		if ( error != SYS_OK)
			return error;
	}
	
	return SYS_OK;
}

FRAMEWORK_INIT_REGISTER(
    Device_InitAll,
    INIT_LEVEL_DEVICE
);

SysResult_t Device_Register(Device_t *dev)
{
	FrameworkListNode_t *pos;	
	Device_t *existing;
	
	//检查参数
	if ( dev == NULL)
		return SYS_ERR_INVALID_ARG;
	
	if ( dev->name == NULL || dev->name[0] == '\0')
		return SYS_ERR_INVALID_ARG;
	
	//检查是否重复注册
	for (pos = s_device_head.next;
		 pos != &s_device_head;
		 pos = pos->next)
	{
		existing = 
		FRAMEWORK_CONTAINER_OF(pos, Device_t, node);
				
		if (dev == existing ||
			strcmp(dev->name, existing->name) == 0)
		{
			return SYS_ERR_ALREADY_EXISTS;
		}
	} 
	
	//// 首次注册要求设备尚未初始化硬件
	if (dev->state != DEVICE_STATE_UNINIT)
	{
		return SYS_ERR_INVALID_STATE;
	}	
	
	//初始化，并加入设备链表节点
	Framework_ListInit(&dev->node);
	Framework_ListAddTail(&s_device_head ,&dev->node);
	
	return SYS_OK;
}

Device_t *Device_Find(const char *name)
{
	FrameworkListNode_t *pos;	
	Device_t *existing;	
	
	//检查参数
	if ( name == NULL || name[0] == '\0')
		return NULL;	
	
	//查找设备
	for (pos = s_device_head.next;
		 pos != &s_device_head;
		 pos = pos->next)
	{
		existing = 
		FRAMEWORK_CONTAINER_OF(pos, Device_t, node);
				
		if (strcmp(name, existing->name) == 0)
		{
			return existing;
		}
	}

	return NULL;
}

SysResult_t Device_Init(Device_t *dev)
{
	Device_t *existing;	
	SysResult_t result;
	
	//检查参数
	if ( dev == NULL )
		return SYS_ERR_INVALID_ARG;
	if (dev->name == NULL || dev->name[0] == '\0')
		return SYS_ERR_INVALID_ARG;	
	
	//查找设备
	existing = Device_Find(dev->name);
	
	if ( existing == NULL)
		return SYS_ERR_NOT_FOUND;
	
	if (existing != dev)
	{
		return SYS_ERR_NOT_FOUND;
	}
	
	//防止生命周期重入
	if (existing->state == DEVICE_STATE_BUSY)
		return SYS_ERR_BUSY;
	
	//// 检查设备生命周期状态
	if ( existing->state != DEVICE_STATE_UNINIT)
		return SYS_ERR_INVALID_STATE;
	
	if ( existing->lifecycle == NULL      ||
		 existing->lifecycle->init == NULL)
		return SYS_ERR_NOT_SUPPORTED;
	
	//防止生命周期重入
	existing->state = DEVICE_STATE_BUSY;
	
	//进行初始化
	result = existing->lifecycle->init(dev);

	//设置正确的设备状态
	existing->state = (result == SYS_OK)
           ? DEVICE_STATE_READY
           : DEVICE_STATE_ERROR;
	
	return result;	
}

SysResult_t Device_Start(Device_t *dev)
{
	Device_t *existing;	
	SysResult_t result;
	
	//检查参数
	if ( dev == NULL)
		return SYS_ERR_INVALID_ARG;
	if (dev->name == NULL || dev->name[0] == '\0')
		return SYS_ERR_INVALID_ARG;	
	
	//查找设备
	existing = Device_Find(dev->name);
	
	if ( existing == NULL)
		return SYS_ERR_NOT_FOUND;
	
	if (existing != dev)
	{
		return SYS_ERR_NOT_FOUND;
	}
	
	//防止生命周期重入
	if (existing->state == DEVICE_STATE_BUSY)
		return SYS_ERR_BUSY;
	
	//// 检查设备生命周期状态
	if ( existing->state != DEVICE_STATE_READY)
		return SYS_ERR_INVALID_STATE;
	
	if ( existing->lifecycle == NULL      ||
		 existing->lifecycle->start == NULL)
		return SYS_ERR_NOT_SUPPORTED;
	
	//防止生命周期重入
	existing->state = DEVICE_STATE_BUSY;
	
	////调用具体设备的启动回调
	result = existing->lifecycle->start(existing);

	//设置正确的设备状态
	existing->state = (result == SYS_OK)
           ? DEVICE_STATE_RUNNING
           : DEVICE_STATE_ERROR;
	
	return result;		
}

SysResult_t Device_Stop(Device_t *dev)
{
	Device_t *existing;	
	SysResult_t result;
	
	//检查参数
	if ( dev == NULL)
		return SYS_ERR_INVALID_ARG;
	if (dev->name == NULL || dev->name[0] == '\0')
		return SYS_ERR_INVALID_ARG;	
	
	//查找设备
	existing = Device_Find(dev->name);
	
	if ( existing == NULL)
		return SYS_ERR_NOT_FOUND;
	
	if (existing != dev)
	{
		return SYS_ERR_NOT_FOUND;
	}
	
	//防止生命周期重入
	if (existing->state == DEVICE_STATE_BUSY)
		return SYS_ERR_BUSY;
	
	//// 检查设备生命周期状态
	if ( existing->state != DEVICE_STATE_RUNNING)
		return SYS_ERR_INVALID_STATE;
	
	if ( existing->lifecycle == NULL      ||
		 existing->lifecycle->stop == NULL)
		return SYS_ERR_NOT_SUPPORTED;
	
	//防止生命周期重入
	existing->state = DEVICE_STATE_BUSY;
	
	////调用具体设备的停止回调
	result = existing->lifecycle->stop(existing);

	//设置正确的设备状态
	existing->state = (result == SYS_OK)
           ? DEVICE_STATE_READY
           : DEVICE_STATE_ERROR;
	
	return result;		
}
