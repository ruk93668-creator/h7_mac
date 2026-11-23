#ifndef CM7_NIMBLE_NPL_OS_H
#define CM7_NIMBLE_NPL_OS_H
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#define BLE_NPL_OS_ALIGNMENT 4
#define BLE_NPL_TIME_FOREVER UINT32_MAX
typedef uint32_t ble_npl_time_t;
typedef int32_t ble_npl_stime_t;
struct ble_npl_event { bool queued; ble_npl_event_fn *fn; void *arg; struct ble_npl_event *next; };
struct ble_npl_eventq { struct ble_npl_event *head,*tail; StaticQueue_t storage; QueueHandle_t wake; uint8_t byte; };
struct ble_npl_mutex { StaticQueue_t storage; QueueHandle_t token; uint8_t byte; TaskHandle_t owner; uint32_t depth; };
struct ble_npl_sem { StaticQueue_t storage; QueueHandle_t tokens; uint8_t bytes[128]; };
struct ble_npl_callout { struct ble_npl_callout *next; struct ble_npl_eventq *queue; struct ble_npl_event event; uint32_t expiry; bool active; };
void BleNpl_PollCallouts(void);
#endif
