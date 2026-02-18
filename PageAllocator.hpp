/**
 * @file PageAllocator.hpp
 * @brief Страничный аллокатор одной зоны памяти (POD, trivially constructible).
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include "AllocConf.h"
#include "AllocTypes.h"
#include "PageBitmap.hpp"
#include "Quarantine.hpp"

namespace AllocCustom {

/**
 * @brief Страничный аллокатор для одной непрерывной зоны.
 *
 * Выделяет память страницами по ALLOC_PAGE_SIZE байт.
 * Каждая область обрамляется хедером и футером.
 * Освобождённые области помещаются в карантин.
 *
 * POD-тип: zero-init из BSS безопасен. Инициализация — через init().
 * НЕ выполняет собственную синхронизацию.
 */
struct PageAllocator {
    /* ── Состояние зоны ── */
    uint8_t* baseAddress;
    size_t   regionSize;
    uint16_t totalPages;
    uint8_t  zoneIndex;
    bool     initialized;

    /* ── Битовые карты ── */
    PageBitmap bitmapInUse;      /**< 1 = занято/карантин, 0 = свободно */
    PageBitmap bitmapAllocated;  /**< 1 = занято, 0 = карантин/свободно */

    /* ── Карантин ── */
    QuarantineTable quarantine;

    /* ── Статистика ── */
    uint32_t sequenceCounter;
    size_t   freePagesCount;
    size_t   minEverFreePages;
    size_t   successfulAllocs;
    size_t   successfulFrees;

    /* ── Основные операции ── */

    void  init(uint8_t* start, size_t size, uint8_t zone);
    void* allocate(size_t requestedSize);
    void  deallocate(void* userPtr);
    void* calloc(size_t num, size_t elemSize);

    /* ── Информация ── */

    size_t freeBytes()        const;
    size_t minEverFreeBytes() const;
    size_t totalBytes()       const;
    size_t usedBytes()        const;
    bool   ownsPointer(const void* userPtr) const;
    bool   isInitialized()    const;

    /* ── Диагностика ── */

    /** Проверить все записи карантина (возвращает false при порче). */
    bool verifyQuarantine() const;

    /** Проверить хедеры/футеры всех аллоцированных областей. */
    bool verifyAllocated() const;

    /** Выполнить все включённые проверки. */
    bool runChecks() const;

private:
    static uint16_t pagesNeeded(size_t requestedSize);

    uint8_t*       pageAddress(uint16_t pageIdx) const;
    int32_t        pageIndex(const void* addr) const;

    void evictFromQuarantine(const AllocQuarantineEntry& entry);
    void updateMpuProtection(uint16_t startPage, uint16_t pageCount);
};

} // namespace AllocCustom
