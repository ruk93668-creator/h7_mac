#ifndef CM7_APP_EVENTS_H
#define CM7_APP_EVENTS_H
#include "runtime_event.h"
#include <stdbool.h>
bool App_EventsIsOwner(RuntimeEventTarget_t target);
void AppTest_EnterCritical(void);
void AppTest_ExitCritical(void);
#define taskENTER_CRITICAL() AppTest_EnterCritical()
#define taskEXIT_CRITICAL() AppTest_ExitCritical()
#endif
