#include "framework_event.h"
#include <stddef.h>



//订阅事务相关
#define EVENT_SUBSCRIPTION_CAPACITY  16U

typedef struct
{
    FrameworkEventId_t id;
    FrameworkEventHandler_t handler;
} EventSubscription_t;

static EventSubscription_t s_subscriptions[EVENT_SUBSCRIPTION_CAPACITY];
static uint16_t s_subscription_count;

//实现 EVENT FIFO QUEUE
#define EVENT_QUEUE_CAPACITY  		 16U
#define DISPATCH_EVENT_MAX 			 4U
typedef struct
{
    FrameworkEvent_t buffer[EVENT_QUEUE_CAPACITY];

    uint16_t head;
    uint16_t tail;
    uint16_t count;
} EventQueue_t;

static EventQueue_t s_event;

//订阅事件，注册回调处理函数
SysResult_t Framework_EventSubscribe(
    FrameworkEventId_t id,
    FrameworkEventHandler_t handler
)
{
	//空函数指针不能注册
	if (handler == NULL)
		return SYS_ERR_INVALID_ARG;
	
	//遍历已注册订阅者
	for (uint16_t i = 0; 
		i < s_subscription_count;	
		i ++)
	{
		if ( s_subscriptions[i].handler == handler &&
			 s_subscriptions[i].id      == id)
			return SYS_ERR_ALREADY_EXISTS;
	}
	
	if ( s_subscription_count >= EVENT_SUBSCRIPTION_CAPACITY)
		return SYS_ERR_NO_RESOURCE;
	
	//注册订阅
	s_subscriptions[s_subscription_count].handler = handler;
	s_subscriptions[s_subscription_count].id      = id;
	s_subscription_count++;
	
	return SYS_OK;
}

//把事件复制到环形队列
SysResult_t Framework_EventPost(
    const FrameworkEvent_t *event
)
{
	if (event == NULL)
		return SYS_ERR_INVALID_ARG;
	
	if ( s_event.count >= EVENT_QUEUE_CAPACITY)
		return SYS_ERR_NO_RESOURCE;
	
	s_event.buffer[s_event.tail] = *event;
	s_event.tail = (s_event.tail + 1)%EVENT_QUEUE_CAPACITY;
	s_event.count++;
	
	return SYS_OK;
}

void Framework_EventDispatch(void)
{
	FrameworkEvent_t event;
	//一次EventDispatch最多处理DISPATH_EVENT_MAX个事件
	for (uint16_t dispatch_count = 0; 
		dispatch_count < DISPATCH_EVENT_MAX ;	
		dispatch_count ++)
	{
		if ( s_event.count == 0)
			return ;
		
		//复制事件+出队
		event = s_event.buffer[s_event.head];
		s_event.head = (s_event.head + 1)%EVENT_QUEUE_CAPACITY; 
		s_event.count--;
		
		//遍历订阅队列,调用回调处理函数
		for (uint16_t subscriber_index = 0; 
			subscriber_index < s_subscription_count;	
			subscriber_index ++)
		{
			if ( s_subscriptions[subscriber_index].id == event.id)
				 s_subscriptions[subscriber_index].handler(&event);
		}		
	}

}
