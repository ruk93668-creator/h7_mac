#include "framework_runtime.h"
#include "framework_timer.h"
#include "framework_event.h"

void Framework_Run(void)
{
    Framework_EventDispatch();
	Framework_TimerProcess();
}
