#ifndef CM7_NIMBLE_NPL_OS_LOG_H
#define CM7_NIMBLE_NPL_OS_LOG_H
void BleNpl_Log(const char *format);
#define BLE_NPL_LOG_IMPL(lvl) \
 static inline void _BLE_NPL_LOG_CAT(BLE_NPL_LOG_MODULE, _BLE_NPL_LOG_CAT(_, lvl))(const char *fmt, ...) { BleNpl_Log(fmt); }
#endif
