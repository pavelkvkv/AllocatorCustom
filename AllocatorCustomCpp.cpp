/**
 * @file AllocatorCustomCpp.cpp
 * @brief Реализация мультизонного аллокатора + C-мост FreeRTOS.
 */
#include "AllocatorCustomCpp.hpp"
#include "FreeRTOSHeapBridge.h"

#include <cstring>
#include <algorithm>
#include <type_traits>

#ifdef HOST_BUILD
#include <mutex>
#else
#include "task.h"
#endif

/* ─────────────────── Глобальный экземпляр ─────────────────── */

namespace {
    AllocCustom::AllocatorCustomCpp g_allocator;

#ifdef HOST_BUILD
    std::mutex g_allocMutex;
#endif
} // namespace

static_assert(std::is_trivially_constructible<AllocCustom::AllocatorCustomCpp>::value,
              "AllocatorCustomCpp must be trivially constructible for BSS placement");

/* ═══════════════════ AllocatorCustomCpp ═══════════════════ */

namespace AllocCustom {

/* ───────── Синхронизация ───────── */

void AllocatorCustomCpp::lock() {
#ifdef HOST_BUILD
    g_allocMutex.lock();
#else
    vTaskSuspendAll();
#endif
}

void AllocatorCustomCpp::unlock() {
#ifdef HOST_BUILD
    g_allocMutex.unlock();
#else
    (void)xTaskResumeAll();
#endif
}

void AllocatorCustomCpp::assertNotISR() const {
#ifndef HOST_BUILD
    uint32_t ipsr;
    __asm volatile("mrs %0, ipsr" : "=r"(ipsr));
    ALLOC_ASSERT(ipsr == 0U && "Аллокатор нельзя вызывать из контекста прерывания");
#endif
}

/* ───────── Инициализация ───────── */

void AllocatorCustomCpp::defineHeapRegions(const HeapRegion_t* regions) {
    if (regions == nullptr) return;

    activeZones_ = 0U;
    currentZone_ = HEAP_ZONE_ANY;
    initialized_ = false;

    const HeapRegion_t* cur = regions;
    while (activeZones_ < ALLOC_MAX_ZONES &&
           cur->pucStartAddress != nullptr &&
           cur->xSizeInBytes != 0U) {
        zones_[activeZones_].init(
            static_cast<uint8_t*>(cur->pucStartAddress),
            cur->xSizeInBytes,
            activeZones_);
        ++activeZones_;
        ++cur;
    }

    ALLOC_ASSERT(activeZones_ > 0U);
    initialized_ = true;
}

void AllocatorCustomCpp::resetState() {
    lock();
    for (uint8_t i = 0; i < activeZones_; ++i) {
        std::memset(&zones_[i], 0, sizeof(PageAllocator));
    }
    activeZones_ = 0U;
    currentZone_ = HEAP_ZONE_ANY;
    initialized_ = false;
    unlock();
}

/* ───────── Маршрутизация зон ───────── */

AllocatorCustomCpp::ZoneRoute
AllocatorCustomCpp::resolveRoute(HeapZone_t zone) const {
    ZoneRoute r{0, 1, true};
    switch (zone) {
        case HEAP_ZONE_FAST:
            r = {0, 0, false};
            break;
        case HEAP_ZONE_SLOW:
            r = {1, 1, false};
            break;
        case HEAP_ZONE_FAST_PREFER:
            r = {0, 1, true};
            break;
        case HEAP_ZONE_SLOW_PREFER:
            r = {1, 0, true};
            break;
        case HEAP_ZONE_ANY:
        default:
            r = {0, 1, true};
            break;
    }
    return r;
}

void* AllocatorCustomCpp::allocateWithRoute(const ZoneRoute& route, size_t size) {
    /* Попытка в primary */
    if (route.primary < activeZones_ && zones_[route.primary].isInitialized()) {
        void* p = zones_[route.primary].allocate(size);
        if (p != nullptr) return p;
    }

    /* Попытка в secondary */
    if (route.trySecondary &&
        route.secondary < activeZones_ &&
        route.secondary != route.primary &&
        zones_[route.secondary].isInitialized()) {
        void* p = zones_[route.secondary].allocate(size);
        if (p != nullptr) return p;
    }

    /* Перебор остальных зон */
    for (uint8_t i = 0; i < activeZones_; ++i) {
        if (i == route.primary) continue;
        if (route.trySecondary && i == route.secondary) continue;
        if (!zones_[i].isInitialized()) continue;

        void* p = zones_[i].allocate(size);
        if (p != nullptr) return p;
    }

    return nullptr;
}

/* ───────── Аллокация ───────── */

void* AllocatorCustomCpp::allocate(size_t size) {
    assertNotISR();
    lock();
    const ZoneRoute route = resolveRoute(currentZone_);
    void* result = allocateWithRoute(route, size);
    unlock();
    return result;
}

void AllocatorCustomCpp::deallocate(void* ptr) {
    if (ptr == nullptr) return;
    assertNotISR();
    lock();

    for (uint8_t i = 0; i < activeZones_; ++i) {
        if (zones_[i].isInitialized() && zones_[i].ownsPointer(ptr)) {
            zones_[i].deallocate(ptr);
            unlock();
            return;
        }
    }

    ALLOC_ASSERT(!"Указатель не принадлежит известным зонам кучи");
    unlock();
}

void* AllocatorCustomCpp::calloc(size_t num, size_t size) {
    assertNotISR();
    lock();
    const ZoneRoute route = resolveRoute(currentZone_);

    void* result = nullptr;
    /* calloc через route с fallback */
    if (route.primary < activeZones_ && zones_[route.primary].isInitialized()) {
        result = zones_[route.primary].calloc(num, size);
    }
    if (result == nullptr && route.trySecondary &&
        route.secondary < activeZones_ &&
        route.secondary != route.primary &&
        zones_[route.secondary].isInitialized()) {
        result = zones_[route.secondary].calloc(num, size);
    }

    unlock();
    return result;
}

/* ───────── Статистика ───────── */

size_t AllocatorCustomCpp::getFreeHeapSize() {
    lock();
    size_t total = 0U;
    for (uint8_t i = 0; i < activeZones_; ++i) {
        total += zones_[i].freeBytes();
    }
    unlock();
    return total;
}

size_t AllocatorCustomCpp::getMinimumEverFreeBytes() {
    lock();
    size_t total = 0U;
    for (uint8_t i = 0; i < activeZones_; ++i) {
        total += zones_[i].minEverFreeBytes();
    }
    unlock();
    return total;
}

void AllocatorCustomCpp::getHeapStats(HeapStats_t* stats) {
    if (stats == nullptr) return;
    std::memset(stats, 0, sizeof(HeapStats_t));

    lock();
    for (uint8_t i = 0; i < activeZones_; ++i) {
        stats->xAvailableHeapSpaceInBytes        += zones_[i].freeBytes();
        stats->xMinimumEverFreeBytesRemaining    += zones_[i].minEverFreeBytes();
        stats->xNumberOfSuccessfulAllocations    += zones_[i].successfulAllocs;
        stats->xNumberOfSuccessfulFrees          += zones_[i].successfulFrees;
    }
    unlock();
}

size_t AllocatorCustomCpp::getTotalHeapSize() {
    lock();
    size_t total = 0U;
    for (uint8_t i = 0; i < activeZones_; ++i) {
        total += zones_[i].totalBytes();
    }
    unlock();
    return total;
}

size_t AllocatorCustomCpp::getUsedHeapSize() {
    return getTotalHeapSize() - getFreeHeapSize();
}

/* ───────── Зоны ───────── */

void       AllocatorCustomCpp::setZone(HeapZone_t zone) { lock(); currentZone_ = zone; unlock(); }
HeapZone_t AllocatorCustomCpp::getZone()          const { return currentZone_; }
uint8_t    AllocatorCustomCpp::getZoneCount()      const { return activeZones_; }
bool       AllocatorCustomCpp::isInitialized()     const { return initialized_; }

size_t AllocatorCustomCpp::getZoneFreeBytes(uint8_t idx) {
    lock();
    size_t r = (idx < activeZones_) ? zones_[idx].freeBytes() : 0U;
    unlock();
    return r;
}

size_t AllocatorCustomCpp::getZoneTotalBytes(uint8_t idx) {
    lock();
    size_t r = (idx < activeZones_) ? zones_[idx].totalBytes() : 0U;
    unlock();
    return r;
}

size_t AllocatorCustomCpp::getZoneMinFreeBytes(uint8_t idx) {
    lock();
    size_t r = (idx < activeZones_) ? zones_[idx].minEverFreeBytes() : 0U;
    unlock();
    return r;
}

size_t AllocatorCustomCpp::getZoneUsedBytes(uint8_t idx) {
    lock();
    size_t r = (idx < activeZones_) ? zones_[idx].usedBytes() : 0U;
    unlock();
    return r;
}

/* ───────── Диагностика ───────── */

bool AllocatorCustomCpp::validateHeap() {
    lock();
    bool ok = true;
    for (uint8_t i = 0; i < activeZones_; ++i) {
        if (!zones_[i].isInitialized()) continue;
        ok = ok && zones_[i].verifyQuarantine();
        ok = ok && zones_[i].verifyAllocated();
    }
    unlock();
    return ok;
}

} // namespace AllocCustom

/* ═══════════════════ C-мост (extern "C") ═══════════════════ */

extern "C" {

void* FreeRTOSHeapInternalAllocate(size_t size) {
    return g_allocator.allocate(size);
}

void FreeRTOSHeapInternalDeallocate(void* ptr) {
    g_allocator.deallocate(ptr);
}

void* FreeRTOSHeapInternalCalloc(size_t num, size_t size) {
    return g_allocator.calloc(num, size);
}

size_t FreeRTOSHeapInternalGetFreeHeapSize(void) {
    return g_allocator.getFreeHeapSize();
}

size_t FreeRTOSHeapInternalGetMinimumEverFreeHeapSize(void) {
    return g_allocator.getMinimumEverFreeBytes();
}

void FreeRTOSHeapInternalGetHeapStats(HeapStats_t* stats) {
    g_allocator.getHeapStats(stats);
}

void FreeRTOSHeapInternalResetState(void) {
    g_allocator.resetState();
}

void vPortDefineHeapRegionsCpp(const HeapRegion_t* pxHeapRegions) {
    g_allocator.defineHeapRegions(pxHeapRegions);
}

void heapZoneSet(HeapZone_t zone) {
    g_allocator.setZone(zone);
}

HeapZone_t heapZoneGet(void) {
    return g_allocator.getZone();
}

UBaseType_t heapZoneGetCount(void) {
    return static_cast<UBaseType_t>(g_allocator.getZoneCount());
}

size_t heapZoneGetFreeBytes(UBaseType_t index) {
    return g_allocator.getZoneFreeBytes(static_cast<uint8_t>(index));
}

size_t heapZoneGetTotalBytes(UBaseType_t index) {
    return g_allocator.getZoneTotalBytes(static_cast<uint8_t>(index));
}

size_t heapZoneGetMinimumFreeBytes(UBaseType_t index) {
    return g_allocator.getZoneMinFreeBytes(static_cast<uint8_t>(index));
}

size_t heapZoneGetUsedBytes(UBaseType_t index) {
    return g_allocator.getZoneUsedBytes(static_cast<uint8_t>(index));
}

} // extern "C"
