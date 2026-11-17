#ifndef CM7_MSC_TEST_CONFIG_H
#define CM7_MSC_TEST_CONFIG_H
//受控测试配置：默认RAM LUN；SD测试必须显式选择MSC启动，默认写保护
#ifndef USB_MSC_SD_TEST
#define USB_MSC_SD_TEST 0
#endif
#ifndef USB_MSC_SD_WRITE_ENABLE
#define USB_MSC_SD_WRITE_ENABLE 0
#endif
#if USB_MSC_SD_TEST && (!defined(USB_MODE_BOOT_MSC) || !USB_MODE_BOOT_MSC)
#error SD_test_requires_explicit_MSC_boot_mode
#endif
#if USB_MSC_SD_WRITE_ENABLE && !USB_MSC_SD_TEST
#error SD_write_requires_controlled_test_mode
#endif
#endif
