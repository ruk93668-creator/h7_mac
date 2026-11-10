#ifndef STAGE_A_CPU_STUB_H
#define STAGE_A_CPU_STUB_H
#include <stdint.h>
#define __PACKED __attribute__((packed))
#define __IO volatile
#define __STATIC_INLINE static __inline
typedef int IRQn_Type;
uint32_t Test_GetPrimask(void);
uint32_t Test_GetBasepri(void);
uint32_t Test_GetIpsr(void);
uint32_t Test_GetPriority(IRQn_Type irq);
void Test_Disable(void);
void Test_Restore(uint32_t value);
void Test_Dmb(void);
#define __get_PRIMASK Test_GetPrimask
#define __get_BASEPRI Test_GetBasepri
#define __get_IPSR Test_GetIpsr
#define NVIC_GetPriority Test_GetPriority
#define __disable_irq Test_Disable
#define __set_PRIMASK Test_Restore
#define __DMB Test_Dmb
#endif
