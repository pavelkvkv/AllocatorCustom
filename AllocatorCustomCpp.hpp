/**
 * @file AllocatorCustomCpp.hpp
 * @brief Мультизонный страничный аллокатор — публичный C++ API.
 *
 * Координирует несколько PageAllocator-ов (по одному на зону).
 * Все публичные методы потокобезопасны.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include "AllocConf.h"
#include "PageAllocator.hpp"

/*
 * FreeRTOS-заголовок нужен для HeapStats_t, HeapRegion_t, UBaseType_t.
 * На хосте — предоставляется стабом.
 */
#include "FreeRTOS.h"
#include "AllocatorZones.h"

namespace AllocCustom {

/**
 * @brief Мультизонный аллокатор с потокобезопасным доступом.
 *
 * POD-совместим (trivially constructible) — важно для статических
 * экземпляров в BSS до вызова конструкторов C++.
 * Мьютекс/планировщик хранятся вне класса (в .cpp).
 */
class AllocatorCustomCpp {
public:
    /* ── Инициализация ── */

    void defineHeapRegions(const HeapRegion_t* regions);
    void resetState();

    /* ── Аллокация (потокобезопасно) ── */

    void* allocate(size_t size);
    void  deallocate(void* ptr);
    void* calloc(size_t num, size_t size);

    /* ── Статистика (потокобезопасно) ── */

    size_t getFreeHeapSize();
    size_t getMinimumEverFreeBytes();
    void   getHeapStats(HeapStats_t* stats);
    size_t getTotalHeapSize();
    size_t getUsedHeapSize();

    /* ── Зоны ── */

    void       setZone(HeapZone_t zone);
    HeapZone_t getZone() const;
    uint8_t    getZoneCount() const;

    size_t getZoneFreeBytes(uint8_t index);
    size_t getZoneTotalBytes(uint8_t index);
    size_t getZoneMinFreeBytes(uint8_t index);
    size_t getZoneUsedBytes(uint8_t index);

    /* ── Диагностика ── */

    /** Валидация всех зон (карантин + аллоцированные области). */
    bool validateHeap();

    bool isInitialized() const;

private:
    PageAllocator zones_[ALLOC_MAX_ZONES];
    uint8_t       activeZones_;
    HeapZone_t    currentZone_;
    bool          initialized_;

    struct ZoneRoute {
        uint8_t primary;
        uint8_t secondary;
        bool    trySecondary;
    };

    ZoneRoute resolveRoute(HeapZone_t zone) const;
    void*     allocateWithRoute(const ZoneRoute& route, size_t size);

    void lock();
    void unlock();
    void assertNotISR() const;
};

} // namespace AllocCustom

extern "C" {
/** Вспомогательная функция инициализации зон из C-кода. */
void vPortDefineHeapRegionsCpp(const HeapRegion_t* pxHeapRegions);
}
