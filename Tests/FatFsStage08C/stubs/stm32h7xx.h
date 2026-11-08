#ifndef FATFS_TEST_CORE_H
#define FATFS_TEST_CORE_H
#include <stdint.h>
extern uint32_t test_ipsr, test_primask, test_basepri;
#define __get_IPSR() (test_ipsr)
#define __get_PRIMASK() (test_primask)
#define __get_BASEPRI() (test_basepri)
#define __disable_irq() (test_primask = 1U)
#define __set_PRIMASK(value) (test_primask = (value))
#endif
