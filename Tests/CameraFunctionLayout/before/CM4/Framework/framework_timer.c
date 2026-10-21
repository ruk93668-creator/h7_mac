/*
 * Framework 软件定时器实现
 *
 * 使用固定对象池保存定时器，使用最小堆优先处理截止时间最早的活动定时器。
 * 上层通过包含池索引及代数的 Handle 操作对象；时间由 Platform_TimeGetMs 提供。
 * 回调在 Framework_TimerProcess 中同步执行。
 *
 * 对外接口：
 * Framework_TimerCreate：校验配置和输出指针，分配池槽位并返回 Handle；创建后未启动。
 * Framework_TimerStart：从当前时间开始计时；加入最小堆或调整已活动定时器的位置。
 * Framework_TimerStop：移出最小堆并停止计时，保留对象及 Handle，允许再次启动。
 * Framework_TimerDelete：必要时停止定时器，释放池槽位，使原 Handle 失效。
 * Framework_TimerProcess：每次最多处理一个到期定时器；单次模式先停止，
 *                         周期模式先从当前时间重新计时，然后执行回调。
 *                         需由运行循环持续调用，向回调传入配置中的 context。
 *
 * 最小堆辅助函数（仅供本文件使用）：
 * Timer_HeapSwap：交换两个堆节点中的池索引，同步对象中的 heap_index。
 * Timer_HeapSiftUp：向父节点方向调整指定节点，恢复最小堆顺序。
 * Timer_HeapSiftDown：向较早到期的子节点方向调整指定节点，恢复最小堆顺序。
 * Timer_HeapRemove：移除节点，清除活动状态及堆位置，用末尾节点补位后调整堆。
 *
 * Handle 辅助函数（仅供本文件使用）：
 * Timer_HandleEncode：校验索引及代数，编码为 32 位 Handle；非法参数返回无效 Handle。
 * Timer_HandleGetIndex：提取 Handle 低 8 位的池索引，不负责有效性校验。
 * Timer_HandleGetGeneration：提取 Handle 高 24 位的代数，不负责有效性校验。
 * Timer_HandleValidate：校验 Handle、索引、分配状态和代数；成功返回对象指针，
 *                       失败返回 NULL，用于拒绝已删除或已被重新分配的旧 Handle。
 *
 * 时间辅助函数（仅供本文件使用）：
 * Timer_DeadlineEarlier：判断截止时间 a 是否早于 b，相等时返回 false。
 * Timer_DeadlineReached：判断当前时间是否已到达或超过截止时间。
 * 两者用无符号差值处理 32 位毫秒计时回绕，比较的实际间隔须小于 2^31 毫秒。
 */

#include "framework_timer.h"
#include <stddef.h>
#include "platform_time.h"
#include "sys_result.h"
#include <stdbool.h>

#define TIMER_POOL_MAX 			 16U
#define TIMER_HEAP_INDEX_INVALID UINT8_MAX
#define	HEAP_TOP 				 0U

typedef struct FrameworkTimer{
	FrameworkTimerConfig_t config;
	
	uint32_t deadline_ms;
	
	bool active, allocated;
	
	uint8_t heap_index;
	
	uint32_t generation;
}FrameworkTimer_t;

static FrameworkTimer_t s_timer_pool[TIMER_POOL_MAX];
static uint8_t s_timer_heap[TIMER_POOL_MAX];
static uint8_t s_heap_size;
static uint8_t s_timer_pool_count;

//实现最小堆
static void Timer_HeapSwap(uint8_t index_a, uint8_t index_b);
static void Timer_HeapSiftUp(uint8_t index);
static void Timer_HeapSiftDown(uint8_t index);
static void Timer_HeapRemove(uint8_t index);
/*
 * 32 位定时器 Handle 布局：
 * [31:8] generation：用于识别定时器槽位的不同生命周期
 * [ 7:0] index：定时器池索引
 */
#define TIMER_HANDLE_INDEX_BITS  8U
#define TIMER_HANDLE_INDEX_MASK  0xFFUL
#define TIMER_HANDLE_GEN_MASK    0x00FFFFFFUL

//Generation Handle 编码与解码
static FrameworkTimerHandle_t Timer_HandleEncode(
    uint8_t index,
    uint32_t generation);
static uint8_t Timer_HandleGetIndex(
    FrameworkTimerHandle_t handle);
static uint32_t Timer_HandleGetGeneration(
    FrameworkTimerHandle_t handle);
static FrameworkTimer_t *Timer_HandleValidate(
    FrameworkTimerHandle_t handle);


static bool Timer_DeadlineEarlier(
    uint32_t deadline_a,
    uint32_t deadline_b
);
static bool Timer_DeadlineReached(
    uint32_t now,
    uint32_t deadline
);

SysResult_t Framework_TimerCreate(
		   const FrameworkTimerConfig_t *config,
		   FrameworkTimerHandle_t *out_handle)
{
	uint8_t pool_count;
	FrameworkTimer_t *timer;
	uint32_t next_generation;
	FrameworkTimerHandle_t new_handle;
	//防御
	if ( out_handle == NULL)
		return SYS_ERR_INVALID_ARG;
	
	*out_handle = FRAMEWORK_TIMER_INVALID_HANDLE;	
	if ( config == NULL)
		return SYS_ERR_INVALID_ARG;
	if ( config->callback == NULL) 
		return SYS_ERR_INVALID_ARG;
	if ( config->period_ms == 0U ||
         config->period_ms > TIMER_MAX_PERIOD_MS)
		return SYS_ERR_INVALID_ARG;
	if ( !(config->mode == FRAMEWORK_TIMER_ONE_SHOT ||
		   config->mode == FRAMEWORK_TIMER_PERIODIC))
		return SYS_ERR_INVALID_ARG;
	if ( s_timer_pool_count >= TIMER_POOL_MAX)
		return SYS_ERR_NO_RESOURCE;
	
	//寻找空余&可使用槽位
	for ( pool_count = 0;
		  pool_count < TIMER_POOL_MAX;
		  pool_count++)
		if (s_timer_pool[pool_count].allocated == false &&
			s_timer_pool[pool_count].generation <
			TIMER_HANDLE_GEN_MASK)
			break;
		
	if (pool_count >= TIMER_POOL_MAX)
    return SYS_ERR_NO_RESOURCE;	
	
	//取出槽位
	timer =
    &s_timer_pool[pool_count];	
	
	//提前编码校验
	next_generation = timer->generation + 1U;

	new_handle = Timer_HandleEncode(
		pool_count,
		next_generation
	);
		
	if (new_handle == FRAMEWORK_TIMER_INVALID_HANDLE)
	{
		return SYS_ERR_INVALID_STATE;
	}

	//写入config
	timer->config = *config;
	timer->deadline_ms = 0U;
	timer->active = false;
	timer->heap_index = TIMER_HEAP_INDEX_INVALID;
	
	timer->generation = next_generation;
	
	////槽位开启
	timer->allocated = true;
	
	//返回
	*out_handle = new_handle;
	s_timer_pool_count++;
	
	
	return SYS_OK;
}

SysResult_t Framework_TimerStart(
    FrameworkTimerHandle_t handle)
{
    FrameworkTimer_t *timer;
    uint32_t now;

    // 校验 Handle，并获取对应的定时器对象
    timer = Timer_HandleValidate(handle);
    if (timer == NULL)
        return SYS_ERR_NOT_FOUND;
		
	if ( timer->active == false && s_heap_size >= TIMER_POOL_MAX)
		return SYS_ERR_NO_RESOURCE;
	
    now = Platform_TimeGetMs();
	
    // 从当前时间重新计算截止时间
    timer->deadline_ms = now + timer->config.period_ms;

    if (timer->active == true)
    {
        // 活动定时器重新计时后，向下调整以恢复最小堆顺序
        Timer_HeapSiftDown(timer->heap_index);
        return SYS_OK;
    }

    // 将定时器池索引追加到堆尾，并记录其堆位置
    s_timer_heap[s_heap_size] = Timer_HandleGetIndex(handle);
    timer->heap_index = s_heap_size;

    // 标记为活动状态，并将新节点计入堆大小
    timer->active = true;
    s_heap_size++;

    // 将新增节点上浮，恢复最小堆顺序
    Timer_HeapSiftUp(timer->heap_index);

    return SYS_OK;
}

//只是暂停定时器，不从timer_pool中删除
SysResult_t Framework_TimerStop(
		   FrameworkTimerHandle_t handle)
{
	FrameworkTimer_t *timer;
	
	timer = Timer_HandleValidate(handle);
	
	if ( timer == NULL)
		return SYS_ERR_NOT_FOUND;
	
	if ( timer->active == false)
		return SYS_OK;
	
	//退出最小堆
	Timer_HeapRemove(timer->heap_index);
	return SYS_OK;
}

//删除定时器
SysResult_t Framework_TimerDelete(
		   FrameworkTimerHandle_t handle)
{
	FrameworkTimer_t *timer;
	
	timer = Timer_HandleValidate(handle);
	
	if ( timer == NULL)
		return SYS_ERR_NOT_FOUND;
		
	//退出最小堆
	if ( timer->active == true)
		Timer_HeapRemove(timer->heap_index);
	
	//退出timer_pool
	timer->allocated = false;
	s_timer_pool_count--;
	
	return SYS_OK;
}

void Framework_TimerProcess(void)
{
	uint32_t now;
	uint8_t pool_index;

	FrameworkTimer_t *timer;
	FrameworkTimerHandle_t handle;

	FrameworkTimerCallback_t callback;
	FrameworkTimerMode_t mode;
	void *context;
	
	if( s_heap_size == 0u)
		return;
	
	pool_index = s_timer_heap[HEAP_TOP];
	timer = &s_timer_pool[pool_index];	
	
	now = Platform_TimeGetMs();

	//最近的timer没有到期
	if (  !Timer_DeadlineReached( now, 
		  timer->deadline_ms))
		return ;
	
	/* 在修改最小堆之前，保存本次回调所需信息 */
	callback = timer->config.callback;
	context  = timer->config.context;
	mode     = timer->config.mode;
		
	//解算handle
	handle = Timer_HandleEncode(
		pool_index,
		timer->generation
	);	
	
	switch(mode)
	{
		case FRAMEWORK_TIMER_ONE_SHOT:
		{
			Framework_TimerStop(handle);
			break;
		}
		case FRAMEWORK_TIMER_PERIODIC:
		{
			Framework_TimerStart(handle);
			break;
		}
	}
	
	//执行回调函数	
	callback(context);
}

/*
 * 定时器内部辅助函数
 *
 * 维护最小堆及定时器对象的堆位置；
 * 编码、解码和校验 Handle；
 * 比较截止时间并判断是否到期，兼容毫秒计时回绕。
 */

/* 实现最小堆 */
static void Timer_HeapSwap(uint8_t index_a, uint8_t index_b)
{
	// 检查两个索引是否在有效堆范围内
	if ( index_a >= s_heap_size ||
		 index_b >= s_heap_size)
		return;
	
	// 交换两个堆节点保存的定时器池索引
	uint8_t temp_heap = s_timer_heap[index_a];
	s_timer_heap[index_a] = s_timer_heap[index_b];
	s_timer_heap[index_b] = temp_heap;
	
	// 同步定时器池中的反向索引，使其指向交换后的堆位置
	s_timer_pool[s_timer_heap[index_a]].heap_index = index_a;
	s_timer_pool[s_timer_heap[index_b]].heap_index = index_b;
	
}

static void Timer_HeapSiftUp(uint8_t index)
{
    uint8_t parent_node;

    // 无效索引或堆顶节点无需上浮
    if (index >= s_heap_size || index == HEAP_TOP)
        return;

    // 向上调整，直到满足最小堆顺序或到达堆顶
    while (index > HEAP_TOP)
    {
        // 计算父节点索引
        parent_node = (uint8_t)((index - 1U) / 2U);

        // 当前节点晚于父节点，堆顺序已满足
		if (!Timer_DeadlineEarlier(
				s_timer_pool[s_timer_heap[index]].deadline_ms,
				s_timer_pool[s_timer_heap[parent_node]].deadline_ms))
			break;

        // 交换节点，并继续检查上一层
        Timer_HeapSwap(index, parent_node);
        index = parent_node;
    }
}

static void Timer_HeapSiftDown(uint8_t index)
{
    uint8_t left_child, right_child;
	uint8_t smallest;

    // 无效索引
    if (index >= s_heap_size)
        return;
	
	
    for (;;)
    {
		smallest = index;
		
        left_child  = ( index * 2) + 1;
		right_child = ( index * 2) + 2;
		
		if (left_child < s_heap_size)
		{
			// 左子节点更早到期时，将其选为最小节点
			if (Timer_DeadlineEarlier(
					s_timer_pool[s_timer_heap[left_child]].deadline_ms,
					s_timer_pool[s_timer_heap[smallest]].deadline_ms))
			{
				smallest = left_child;
			}
		}

		if (right_child < s_heap_size)
		{
			// 将右子节点与当前选出的最小节点比较
			if (Timer_DeadlineEarlier(
					s_timer_pool[s_timer_heap[right_child]].deadline_ms,
					s_timer_pool[s_timer_heap[smallest]].deadline_ms))
			{
				smallest = right_child;
			}
		}
		
		if ( smallest == index)
			return;
		
		Timer_HeapSwap(index, smallest);
		index = smallest;
    }
}

static void Timer_HeapRemove(uint8_t index)
{
	uint8_t pool_index;
	uint8_t last_heap_index;
	uint8_t parent_heap;
	
    // 无效索引
    if (index >= s_heap_size)
        return;
	
	last_heap_index = s_heap_size - 1U;

	//保存要删除的POOL索引
	pool_index = s_timer_heap[index];
	
	if ( index != last_heap_index)
	{
		Timer_HeapSwap(index, last_heap_index);
	}
	
	s_heap_size--;
	//关闭定时器，从堆里面删除
	s_timer_pool[pool_index].active = false;
	s_timer_pool[pool_index].heap_index = TIMER_HEAP_INDEX_INVALID;

	if (index >= s_heap_size)
		return;
	/*处理最小堆，满足最小堆顺序*/
	if (index == HEAP_TOP)
	{
		Timer_HeapSiftDown(index);
		return;
	}
	
	parent_heap = ( index - 1 )/2;
	
	if ( !Timer_DeadlineEarlier(
		s_timer_pool[s_timer_heap[index]].deadline_ms,
		s_timer_pool[s_timer_heap[parent_heap]].deadline_ms
	))
		Timer_HeapSiftDown(index);
	else
		Timer_HeapSiftUp(index);
}

//Handle 编码工具
static FrameworkTimerHandle_t Timer_HandleEncode(
    uint8_t index,
    uint32_t generation
)
{
	if (generation > TIMER_HANDLE_GEN_MASK ||
		generation == 0u ||
		index	   >= TIMER_POOL_MAX)
		return FRAMEWORK_TIMER_INVALID_HANDLE;
	
	return ((generation & TIMER_HANDLE_GEN_MASK) << TIMER_HANDLE_INDEX_BITS)|
		    (index & TIMER_HANDLE_INDEX_MASK );
}

static uint8_t Timer_HandleGetIndex(
    FrameworkTimerHandle_t handle
)
{
	return ( handle & TIMER_HANDLE_INDEX_MASK);
}

static uint32_t Timer_HandleGetGeneration(
    FrameworkTimerHandle_t handle
)
{
	return (handle >> TIMER_HANDLE_INDEX_BITS);
}

static FrameworkTimer_t *Timer_HandleValidate(
    FrameworkTimerHandle_t handle)
{
    uint8_t index;
    uint32_t generation;	
	
	if ( handle == FRAMEWORK_TIMER_INVALID_HANDLE)
		return NULL;
	
	index = Timer_HandleGetIndex(handle);
	generation = Timer_HandleGetGeneration(handle);
	
	if ( index >= TIMER_POOL_MAX)
		return NULL;
	
	if ( s_timer_pool[index].allocated == false)
		return NULL;
	if ( s_timer_pool[index].generation != generation)
		return NULL;
	
	return &s_timer_pool[index];
}

/* 判断截止时间 a 是否早于 b；相等时返回 false */
static bool Timer_DeadlineEarlier(
    uint32_t deadline_a,
    uint32_t deadline_b)
{
    return (uint32_t)(deadline_a - deadline_b)
           > UINT32_C(0x80000000);
}
/* 判断当前时间是否已到达或超过截止时间 */
static bool Timer_DeadlineReached(
    uint32_t now,
    uint32_t deadline)
{
    return (uint32_t)(now - deadline)
           < UINT32_C(0x80000000);
}

