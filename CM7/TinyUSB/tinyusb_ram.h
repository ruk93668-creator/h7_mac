#ifndef H7_TINYUSB_RAM_H
#define H7_TINYUSB_RAM_H
#include <stdbool.h>
/* Call only from the same task that runs tud_task_ext(). */
void TinyUSB_RamPoll(void);
bool TinyUSB_RamDrained(void);
#endif
