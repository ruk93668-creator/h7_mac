//heap_4只在本编译单元启用动态分配，并重命名所有公共符号；不改变全局FreeRTOSConfig
#include "ble_heap.h"
#define configTOTAL_HEAP_SIZE (16U * 1024U)
#define pvPortMalloc BleHeap_RawAlloc
#define vPortFree BleHeap_RawFree
#define xPortGetFreeHeapSize BleHeap_FreeBytes
#define xPortGetMinimumEverFreeHeapSize BleHeap_MinBytes
#define vPortGetHeapStats BleHeap_GetStats
#define vPortInitialiseBlocks BleHeap_InitialiseBlocks
#include "FreeRTOS.h"
#include "task.h"
#undef configSUPPORT_DYNAMIC_ALLOCATION
#define configSUPPORT_DYNAMIC_ALLOCATION 1
#include "../../../Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_4.c"
#include "stm32h7xx.h"
#include <string.h>
#include <stdint.h>
//前缀保持8字节对齐，realloc只复制原申请范围；仅用于BLE源码
struct BleAllocation { size_t size; uint32_t marker; };
void *BleHeap_Alloc(size_t size)
{
    struct BleAllocation *p;
    if(__get_IPSR()!=0U || __get_PRIMASK()!=0U || size==0U || size>SIZE_MAX-sizeof(*p)) return NULL;
    p=BleHeap_RawAlloc(size+sizeof(*p));
    if(p==NULL) return NULL;
    p->size=size;p->marker=0x424c4548U;return p+1;
}
void BleHeap_Free(void *pointer)
{
    struct BleAllocation *p;
    if(pointer==NULL) return;
    configASSERT(__get_IPSR()==0U && __get_PRIMASK()==0U);
    p=(struct BleAllocation *)pointer-1;configASSERT(p->marker==0x424c4548U);
    p->marker=0U;BleHeap_RawFree(p);
}
void *BleHeap_Calloc(size_t count,size_t size)
{
    void *p;
    if(size!=0U && count>SIZE_MAX/size) return NULL;
    p=BleHeap_Alloc(count*size);if(p!=NULL)memset(p,0,count*size);return p;
}
void *BleHeap_Realloc(void *pointer,size_t size)
{
    struct BleAllocation *old;void *p;
    if(pointer==NULL)return BleHeap_Alloc(size);
    if(size==0U){BleHeap_Free(pointer);return NULL;}
    old=(struct BleAllocation *)pointer-1;configASSERT(old->marker==0x424c4548U);
    p=BleHeap_Alloc(size);if(p==NULL)return NULL;
    memcpy(p,pointer,old->size<size?old->size:size);BleHeap_Free(pointer);return p;
}
