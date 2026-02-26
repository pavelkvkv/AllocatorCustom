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
/* Логирование — только на таргете */
#if ALLOC_ENABLE_LOGGING
#define TAG "Alloc"
#define LOG_LEVEL LOG_LEVEL_WARN
#include "log.h"
#endif
/* logF_ISR для трассировки и дампа */
#if ALLOC_TRACE_BUFFER_SIZE > 0 && ALLOC_ENABLE_LOGGING
#include "log_isr.h"
#endif
#endif

/* ─────────────────── Глобальный экземпляр ─────────────────── */

namespace {
    AllocCustom::AllocatorCustomCpp g_allocator;

#ifdef HOST_BUILD
    std::mutex g_allocMutex;
#endif

    /** Получить имя текущего таска (или "???" если недоступно). */
    inline const char* currentTaskName() {
#if !defined(HOST_BUILD) && ALLOC_ENABLE_LOGGING
        TaskHandle_t h = xTaskGetCurrentTaskHandle();
        if (h != nullptr) {
            return pcTaskGetName(h);
        }
#endif
        return "???";
    }
} // namespace

static_assert(std::is_trivially_constructible<AllocCustom::AllocatorCustomCpp>::value,
              "AllocatorCustomCpp must be trivially constructible for BSS placement");

/* ═══════════════════ Трассировка операций ═══════════════════ */

#if ALLOC_TRACE_BUFFER_SIZE > 0

/** Глобальный кольцевой буфер — доступен из отладчика: (gdb) p g_allocTrace */
extern "C" AllocTraceRing g_allocTrace;
AllocTraceRing g_allocTrace;

static void allocTraceRecord(uint32_t size, uint8_t zone, uint8_t op, void* addr) {
    const char* name = "???";
#if !defined(HOST_BUILD)
    TaskHandle_t h = xTaskGetCurrentTaskHandle();
    if (h != nullptr) {
        name = pcTaskGetName(h);
    }
#endif

    AllocTraceEntry& e = g_allocTrace.entries[g_allocTrace.head];
    e.addr      = addr;
    e.size      = size;
    e.zone      = zone;
    e.op        = op;
    e._reserved = 0U;

    /* Копируем имя таска — TCB может быть удалён к моменту чтения буфера */
    {
        size_t i = 0U;
        while (i < ALLOC_TRACE_NAME_LEN - 1U && name[i] != '\0') {
            e.task[i] = name[i];
            ++i;
        }
        e.task[i] = '\0';
    }

    g_allocTrace.head = (g_allocTrace.head + 1U) % ALLOC_TRACE_BUFFER_SIZE;
    ++g_allocTrace.totalOps;

#if !defined(HOST_BUILD) && ALLOC_ENABLE_LOGGING && ALLOC_TRACE_LOG_OPERATIONS
    logF_ISR("%s %uB Z%u %p %s\n",
             (op == 'A') ? "alloc" : "free",
             static_cast<unsigned>(size),
             static_cast<unsigned>(zone),
             addr,
             e.task);
#endif
}

extern "C" void alloc_trace_dump(void) {
    const uint32_t count = (g_allocTrace.totalOps < ALLOC_TRACE_BUFFER_SIZE)
                           ? g_allocTrace.totalOps
                           : static_cast<uint32_t>(ALLOC_TRACE_BUFFER_SIZE);
    if (count == 0U) return;

#if !defined(HOST_BUILD) && ALLOC_ENABLE_LOGGING
    logF_ISR("=== Alloc trace (last %u of %u) ===\n",
             static_cast<unsigned>(count),
             static_cast<unsigned>(g_allocTrace.totalOps));

    uint32_t idx = (g_allocTrace.head + ALLOC_TRACE_BUFFER_SIZE - count)
                   % ALLOC_TRACE_BUFFER_SIZE;

    for (uint32_t i = 0U; i < count; ++i) {
        const AllocTraceEntry& e = g_allocTrace.entries[idx];
        logF_ISR("[%u] %s %uB Z%u %p %s\n",
                 static_cast<unsigned>(i),
                 (e.op == 'A') ? "alloc" : "free",
                 static_cast<unsigned>(e.size),
                 static_cast<unsigned>(e.zone),
                 e.addr,
                 e.task);
        idx = (idx + 1U) % ALLOC_TRACE_BUFFER_SIZE;
    }

    logF_ISR("=== end ===\n");
#endif
}

#endif /* ALLOC_TRACE_BUFFER_SIZE > 0 */

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
#if !defined(HOST_BUILD) && ALLOC_ENABLE_LOGGING && ALLOC_LOG_SMALL_ALLOC_WARN
    /* Пороги по зоне: используем константы до аллокации */
    static constexpr size_t kThreshFast = ALLOC_LOG_SMALL_ALLOC_THRESHOLD_FAST;
    static constexpr size_t kThreshSlow = ALLOC_LOG_SMALL_ALLOC_THRESHOLD_SLOW;
#endif

    /* Попытка в primary */
    if (route.primary < activeZones_ && zones_[route.primary].isInitialized()) {
        void* p = zones_[route.primary].allocate(size);
        if (p != nullptr) {
#if !defined(HOST_BUILD) && ALLOC_ENABLE_LOGGING && ALLOC_LOG_SMALL_ALLOC_WARN
            const size_t thresh = (route.primary == 0U) ? kThreshFast : kThreshSlow;
            if (size > 0U && size < thresh) {
                logW("smallAlloc zone%u: %u Б (thr %u), tsk: %s",
                     route.primary,
                     static_cast<unsigned>(size),
                     static_cast<unsigned>(thresh),
                     currentTaskName());
            }
#endif
            return p;
        }
    }

    /* Попытка в secondary */
    if (route.trySecondary &&
        route.secondary < activeZones_ &&
        route.secondary != route.primary &&
        zones_[route.secondary].isInitialized()) {
        void* p = zones_[route.secondary].allocate(size);
        if (p != nullptr) {
#if !defined(HOST_BUILD) && ALLOC_ENABLE_LOGGING
#if ALLOC_LOG_SECONDARY_ZONE_WARN
            logW("fallback zone%u→%u %u Б (primary полна), таск: %s",
                 route.primary, route.secondary,
                 static_cast<unsigned>(size), currentTaskName());
#endif
#if ALLOC_LOG_SMALL_ALLOC_WARN
            const size_t thresh = (route.secondary == 0U) ? kThreshFast : kThreshSlow;
            if (size > 0U && size < thresh) {
                logW("smallAlloc zone%u: %u Б (thr %u), tsk: %s",
                     route.secondary,
                     static_cast<unsigned>(size),
                     static_cast<unsigned>(thresh),
                     currentTaskName());
            }
#endif
#endif
            return p;
        }
    }

    /* Перебор остальных зон (только если разрешён fallback) */
    if (route.trySecondary) {
        for (uint8_t i = 0; i < activeZones_; ++i) {
            if (i == route.primary) continue;
            if (i == route.secondary) continue;
            if (!zones_[i].isInitialized()) continue;

            void* p = zones_[i].allocate(size);
            if (p != nullptr) {
#if !defined(HOST_BUILD) && ALLOC_ENABLE_LOGGING
#if ALLOC_LOG_SECONDARY_ZONE_WARN
                logW("fallback zone%u→%u %u Б (primary+secondary полны), таск: %s",
                     route.primary, i,
                     static_cast<unsigned>(size), currentTaskName());
#endif
#if ALLOC_LOG_SMALL_ALLOC_WARN
                const size_t thresh = (i == 0U) ? kThreshFast : kThreshSlow;
                if (size > 0U && size < thresh) {
                    logW("smallAlloc zone%u: %u Б (thr %u), tsk: %s",
                         i,
                         static_cast<unsigned>(size),
                         static_cast<unsigned>(thresh),
                         currentTaskName());
                }
#endif
#endif
                return p;
            }
        }
    }

    return nullptr;
}

/* ───────── Аллокация ───────── */

void* AllocatorCustomCpp::allocate(size_t size) {
    assertNotISR();
    lock();
    const ZoneRoute route = resolveRoute(currentZone_);
    void* result = allocateWithRoute(route, size);
#if ALLOC_TRACE_BUFFER_SIZE > 0
    if (result != nullptr) {
        for (uint8_t i = 0; i < activeZones_; ++i) {
            if (zones_[i].ownsPointer(result)) {
                allocTraceRecord(static_cast<uint32_t>(size), i, 'A', result);
                break;
            }
        }
    }
#endif
    unlock();
    return result;
}

void AllocatorCustomCpp::deallocate(void* ptr) {
    if (ptr == nullptr) return;
    assertNotISR();
    lock();

    for (uint8_t i = 0; i < activeZones_; ++i) {
        if (zones_[i].isInitialized() && zones_[i].ownsPointer(ptr)) {
#if ALLOC_TRACE_BUFFER_SIZE > 0
            allocTraceRecord(zones_[i].getRequestedSize(ptr), i, 'F', ptr);
#endif
            zones_[i].deallocate(ptr);
            unlock();
            return;
        }
    }

    ALLOC_FAIL("Указатель не принадлежит известным зонам кучи");
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
