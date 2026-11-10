#ifndef STAGE_A_TASK_H
#define STAGE_A_TASK_H
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
typedef enum { eSetBits } eNotifyAction;
#define taskSCHEDULER_NOT_STARTED 0
#define taskSCHEDULER_RUNNING 1
int xTaskGetSchedulerState(void);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
BaseType_t xTaskNotify(TaskHandle_t task,uint32_t bits,eNotifyAction action);
BaseType_t xTaskNotifyFromISR(TaskHandle_t task,uint32_t bits,eNotifyAction action,BaseType_t *wake);
BaseType_t xTaskNotifyWait(uint32_t clear_entry,uint32_t clear_exit,uint32_t *value,TickType_t ticks);
#endif
