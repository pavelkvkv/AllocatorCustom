/**
 * @file AllocatorZones.h
 * @brief Перечисления и вспомогательные функции выбора зон кучи.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Тип выбора зоны для последующих выделений памяти.
 *
 * PREFER — попытка в указанной зоне с откатом в другую.
 */
typedef enum {
    HEAP_ZONE_ANY         = 0,  /**< Авто: fast → slow. */
    HEAP_ZONE_FAST        = 1,  /**< Только быстрая зона. */
    HEAP_ZONE_SLOW        = 2,  /**< Только медленная зона. */
    HEAP_ZONE_FAST_PREFER = 3,  /**< Быстрая с откатом. */
    HEAP_ZONE_SLOW_PREFER = 4   /**< Медленная с откатом. */
} HeapZone_t;

void        heapZoneSet(HeapZone_t zone);
HeapZone_t  heapZoneGet(void);
UBaseType_t heapZoneGetCount(void);
size_t      heapZoneGetFreeBytes(UBaseType_t index);
size_t      heapZoneGetTotalBytes(UBaseType_t index);
size_t      heapZoneGetMinimumFreeBytes(UBaseType_t index);
size_t      heapZoneGetUsedBytes(UBaseType_t index);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
namespace RegionAlloc {

enum class Zone : uint8_t {
    Any       = static_cast<uint8_t>(HEAP_ZONE_ANY),
    Fast      = static_cast<uint8_t>(HEAP_ZONE_FAST),
    Slow      = static_cast<uint8_t>(HEAP_ZONE_SLOW),
    FastPrefer = static_cast<uint8_t>(HEAP_ZONE_FAST_PREFER),
    SlowPrefer = static_cast<uint8_t>(HEAP_ZONE_SLOW_PREFER),

    /* Обратная совместимость */
    HEAP_ZONE_ANY         = Any,
    HEAP_ZONE_FAST        = Fast,
    HEAP_ZONE_SLOW        = Slow,
    HEAP_ZONE_FAST_PREFER = FastPrefer,
    HEAP_ZONE_SLOW_PREFER = SlowPrefer
};

} // namespace RegionAlloc
#endif
