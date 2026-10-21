/*
 * Framework 软件定时器公共接口，供 CM7 和 CM4 的上层模块使用。
 * CM7 和 CM4 分别维护独立的 Timer 对象池。
 * Timer Handle 仅在创建它的核心内部有效。
 * 不支持直接通过 IPC 将 Handle 交给另一核心操作。
 * 定义单次/周期模式、定时器配置、回调函数及 Handle 类型。
 * 配置包含周期（毫秒）、运行模式、回调函数及传入回调的 context。
 * 调用者通过 Handle 管理定时器，无需访问内部对象池和最小堆。
 *
 * 对外接口：
 * Framework_TimerCreate  - 按配置创建定时器并返回 Handle，创建后尚未启动。
 * Framework_TimerStart   - 启动定时器；已启动时从当前时间重新计时。
 * Framework_TimerStop    - 停止定时器，保留对象和 Handle，允许再次启动。
 * Framework_TimerDelete  - 释放定时器对象，使原 Handle 失效。
 * Framework_TimerProcess - 用于检查到期定时器并执行回调，由运行循环调用。
 *
 * Create/Start/Stop/Delete 返回 SysResult_t，SYS_OK 表示成功。
 */
 /*
 * 调度模型：
 * 1. Timer 为协作式软件定时器，不直接依赖硬件定时器中断。
 * 2. Framework_TimerProcess() 每次最多处理一个到期 Timer。
 * 3. Callback 在调用 Framework_TimerProcess() 的上下文中同步执行。
 * 4. Callback 应避免阻塞，以免延迟其他 Timer 和事件处理。
 * 5. 当前版本不支持从 ISR 或多个执行上下文并发调用管理接口。
 */
#ifndef FRAMEWORK_TIMER_H
#define FRAMEWORK_TIMER_H

#include <stdint.h>
#include "sys_result.h"

#define TIMER_MAX_PERIOD_MS 86400000UL
#define FRAMEWORK_TIMER_INVALID_HANDLE 0U

typedef enum{
	FRAMEWORK_TIMER_ONE_SHOT,
	FRAMEWORK_TIMER_PERIODIC
}FrameworkTimerMode_t;

//定时器到期回调，context来自创建配置
typedef void (* FrameworkTimerCallback_t)
(void *context);

typedef struct{
	uint32_t period_ms;
	
	FrameworkTimerMode_t mode;
	
	FrameworkTimerCallback_t callback;
	void *context;
}FrameworkTimerConfig_t;

typedef uint32_t FrameworkTimerHandle_t;

//按配置创建定时器并返回Handle，创建后不自动启动
SysResult_t Framework_TimerCreate(
		   const FrameworkTimerConfig_t *config,
		   FrameworkTimerHandle_t *out_handle);

//启动定时器，已启动时从当前时间重新计时
SysResult_t Framework_TimerStart(
		   FrameworkTimerHandle_t handle);

//停止定时器，保留对象和Handle以便再次启动
SysResult_t Framework_TimerStop(
		   FrameworkTimerHandle_t handle);
/*
 * Delete 成功后原 Handle 失效。
 * 调用者应自行将 Handle 设置为
 * FRAMEWORK_TIMER_INVALID_HANDLE。
 */

//删除定时器，使旧Handle失效
SysResult_t Framework_TimerDelete(
		   FrameworkTimerHandle_t handle);

//检查到期定时器，每轮最多处理一个并同步执行回调
void Framework_TimerProcess(void);
		  
#endif /* FRAMEWORK_TIMER_H */
