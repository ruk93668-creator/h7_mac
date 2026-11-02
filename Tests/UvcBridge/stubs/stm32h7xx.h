#ifndef UVC_BRIDGE_TEST_CPU_H
#define UVC_BRIDGE_TEST_CPU_H
#include <stdint.h>
uint32_t Test_GetIpsr(void);
uint32_t Test_GetPrimask(void);
#define __get_IPSR Test_GetIpsr
#define __get_PRIMASK Test_GetPrimask
#endif
