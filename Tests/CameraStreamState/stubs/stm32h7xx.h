#ifndef TEST_STM32H7XX_H
#define TEST_STM32H7XX_H
#include <stdint.h>
#define __get_PRIMASK Test_GetPrimask
#define __disable_irq Test_DisableIrq
#define __set_PRIMASK Test_SetPrimask
uint32_t Test_GetPrimask(void);
void Test_DisableIrq(void);
void Test_SetPrimask(uint32_t value);
#endif
