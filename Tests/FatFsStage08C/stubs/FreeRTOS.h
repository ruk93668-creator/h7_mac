#ifndef FATFS_TEST_FREERTOS_H
#define FATFS_TEST_FREERTOS_H
#include <stdint.h>
typedef void * TaskHandle_t;
typedef int BaseType_t;
typedef uint32_t TickType_t;
typedef struct {uint32_t unused;} StaticSemaphore_t;
typedef void * SemaphoreHandle_t;
#define taskSCHEDULER_RUNNING 2
#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(x) (x)
#define portYIELD_FROM_ISR(x) ((void)(x))
#endif
