#include "FreeRTOS.h"
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *storage);
BaseType_t xSemaphoreTake(SemaphoreHandle_t event, TickType_t ticks);
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t event, BaseType_t *wake);
