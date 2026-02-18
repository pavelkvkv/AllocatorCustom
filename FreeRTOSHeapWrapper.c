/*
 * @file FreeRTOSHeapWrapper.c
 * @brief Тонкие C-обёртки стандартных FreeRTOS heap-функций.
 *
 * Вся синхронизация выполняется внутри AllocatorCustomCpp.
 * Этот файл — чистый passthrough для совместимости с FreeRTOS API.
 */

#include "FreeRTOSHeapBridge.h"

void * pvPortMalloc( size_t xWantedSize )
{
    void * pvReturn = FreeRTOSHeapInternalAllocate( xWantedSize );

#if ( configUSE_MALLOC_FAILED_HOOK == 1 )
    if( pvReturn == NULL )
    {
        extern void vApplicationMallocFailedHook( void );
        vApplicationMallocFailedHook();
    }
#endif

    return pvReturn;
}

void vPortFree( void * pv )
{
    FreeRTOSHeapInternalDeallocate( pv );
}

size_t xPortGetFreeHeapSize( void )
{
    return FreeRTOSHeapInternalGetFreeHeapSize();
}

size_t xPortGetMinimumEverFreeHeapSize( void )
{
    return FreeRTOSHeapInternalGetMinimumEverFreeHeapSize();
}

void vPortInitialiseBlocks( void )
{
    /* Совместимость с heap_4.c — ничего не делаем. */
}

void * pvPortCalloc( size_t xNum, size_t xSize )
{
    return FreeRTOSHeapInternalCalloc( xNum, xSize );
}

void vPortGetHeapStats( HeapStats_t * pxHeapStats )
{
    if( pxHeapStats == NULL )
    {
        return;
    }
    FreeRTOSHeapInternalGetHeapStats( pxHeapStats );
}

void vPortHeapResetState( void )
{
    FreeRTOSHeapInternalResetState();
}

void vPortDefineHeapRegions( const HeapRegion_t * pxHeapRegions )
{
    vPortDefineHeapRegionsCpp( pxHeapRegions );
}
