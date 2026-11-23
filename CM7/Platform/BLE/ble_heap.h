#ifndef CM7_BLE_HEAP_H
#define CM7_BLE_HEAP_H
#include <stddef.h>
void *BleHeap_Alloc(size_t size);
void BleHeap_Free(void *pointer);
void *BleHeap_Calloc(size_t count, size_t size);
void *BleHeap_Realloc(void *pointer, size_t size);
size_t BleHeap_FreeBytes(void);
size_t BleHeap_MinBytes(void);
#endif
