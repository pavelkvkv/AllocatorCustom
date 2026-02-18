/**
 * @file FreeRTOSHeapBridge.h
 * @brief Внутренний C-интерфейс к мультизонному аллокатору.
 */
#pragma once

#include <stddef.h>
#include "FreeRTOS.h"
#include "AllocatorZones.h"

#ifdef __cplusplus
extern "C" {
#endif

void*  FreeRTOSHeapInternalAllocate(size_t size);
void   FreeRTOSHeapInternalDeallocate(void* ptr);
void*  FreeRTOSHeapInternalCalloc(size_t num, size_t size);
size_t FreeRTOSHeapInternalGetFreeHeapSize(void);
size_t FreeRTOSHeapInternalGetMinimumEverFreeHeapSize(void);
void   FreeRTOSHeapInternalGetHeapStats(HeapStats_t* stats);
void   FreeRTOSHeapInternalResetState(void);
void   vPortDefineHeapRegionsCpp(const HeapRegion_t* pxHeapRegions);

#ifdef __cplusplus
}
#endif
