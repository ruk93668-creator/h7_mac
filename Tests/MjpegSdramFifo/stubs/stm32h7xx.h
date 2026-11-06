#ifndef MJPEG_TEST_STM32H7XX_H
#define MJPEG_TEST_STM32H7XX_H
#include <stdint.h>
extern uint32_t mock_primask;
static inline uint32_t __get_PRIMASK(void) { return mock_primask; }
static inline void __disable_irq(void) { mock_primask = 1U; }
static inline void __set_PRIMASK(uint32_t value) { mock_primask = value; }
static inline void __DMB(void) { }
#endif
