/**
 * @file PageAllocator.cpp
 * @brief Реализация страничного аллокатора одной зоны.
 *
 * Метаданные (requestedSize, pageCount, sequenceNum) хранятся в отдельной
 * таблице AllocPageMeta, вырезанной из конца зоны при init().
 * В области выделения — только канарейки произвольного размера.
 */
#include "PageAllocator.hpp"
#include "BlockGuard.hpp"
#include "MpuGuard.hpp"
#include <cstring>

/* Диагностический вывод при обнаружении порчи (ISR-safe, без метаданных) */
#if !defined(HOST_BUILD) && ALLOC_ENABLE_LOGGING
#include "log_isr.h"
#define ALLOC_DIAG(fmt, ...) logF_ISR(fmt "\n", ##__VA_ARGS__)
#else
#define ALLOC_DIAG(fmt, ...) ((void)0)
#endif

namespace AllocCustom {

/* ───────── Проверки конфигурации ───────── */

static_assert(ALLOC_PAGE_SIZE >= ALLOC_HEADER_SIZE + ALLOC_FOOTER_SIZE + 1U,
              "Страница слишком мала: header + footer + 1 байт payload");
static_assert(ALLOC_HEADER_SIZE >= 1U, "Хедер-канарейка ≥ 1 байт");
static_assert(ALLOC_FOOTER_SIZE >= 1U, "Футер-канарейка ≥ 1 байт");

/* ───────── Инициализация ───────── */

void PageAllocator::init(uint8_t* start, size_t size, uint8_t zone) {
    ALLOC_ASSERT(start != nullptr);
    ALLOC_ASSERT(size >= ALLOC_PAGE_SIZE + sizeof(AllocPageMeta));

    baseAddress = start;
    regionSize  = size;
    zoneIndex   = zone;

    /*
     * Разбиваем зону: [страницы][таблица метаданных].
     * Каждая страница «стоит» PAGE_SIZE + sizeof(AllocPageMeta).
     */
    const size_t effectiveCost = ALLOC_PAGE_SIZE + sizeof(AllocPageMeta);
    totalPages = static_cast<uint16_t>(size / effectiveCost);

    ALLOC_ASSERT(totalPages > 0U);
    ALLOC_ASSERT(totalPages <= ALLOC_MAX_PAGES_PER_ZONE);

    /* Мета-таблица размещается сразу после последней страницы */
    metaTable = reinterpret_cast<AllocPageMeta*>(
        start + static_cast<size_t>(totalPages) * ALLOC_PAGE_SIZE);
    std::memset(metaTable, 0, static_cast<size_t>(totalPages) * sizeof(AllocPageMeta));

    bitmapInUse.init(totalPages);
    bitmapAllocated.init(totalPages);
    quarantine.init();

    sequenceCounter  = 0U;
    freePagesCount   = totalPages;
    minEverFreePages = totalPages;
    successfulAllocs = 0U;
    successfulFrees  = 0U;

    initialized = true;
}

/* ───────── Вспомогательные ───────── */

uint16_t PageAllocator::pagesNeeded(size_t requestedSize) {
    const size_t total = ALLOC_HEADER_SIZE + requestedSize + ALLOC_FOOTER_SIZE;
    return static_cast<uint16_t>((total + ALLOC_PAGE_SIZE - 1U) / ALLOC_PAGE_SIZE);
}

uint8_t* PageAllocator::pageAddress(uint16_t pageIdx) const {
    return baseAddress + static_cast<size_t>(pageIdx) * ALLOC_PAGE_SIZE;
}

int32_t PageAllocator::pageIndex(const void* addr) const {
    const auto* ptr = static_cast<const uint8_t*>(addr);
    const uint8_t* end = baseAddress + static_cast<size_t>(totalPages) * ALLOC_PAGE_SIZE;
    if (ptr < baseAddress || ptr >= end) {
        return -1;
    }
    return static_cast<int32_t>((ptr - baseAddress) / ALLOC_PAGE_SIZE);
}

const AllocPageMeta* PageAllocator::getMeta(uint16_t startPage) const {
    ALLOC_ASSERT(startPage < totalPages);
    return &metaTable[startPage];
}

uint32_t PageAllocator::getRequestedSize(const void* userPtr) const {
    if (!initialized || userPtr == nullptr) return 0U;
    const void* ps = BlockGuard::pageFromUserData(userPtr);
    const int32_t pi = pageIndex(ps);
    if (pi < 0) return 0U;
    return metaTable[static_cast<uint16_t>(pi)].requestedSize;
}

/* ───────── Аллокация ───────── */

void* PageAllocator::allocate(size_t requestedSize) {
    if (!initialized || requestedSize == 0U) return nullptr;

    const uint16_t pages = pagesNeeded(requestedSize);
    if (pages > freePagesCount) return nullptr;

    /* Проверки целостности перед операцией */
#if ALLOC_QUARANTINE_CHECK_LEVEL > 0
    if (!verifyQuarantine()) { ALLOC_FAIL("quarantine corrupted on alloc"); }
#endif
#if ALLOC_CHECK_ALL_ALLOCATED
    if (!verifyAllocated()) { ALLOC_FAIL("allocated blocks corrupted on alloc"); }
#endif

    /* Поиск непрерывного свободного участка */
    const int32_t sp32 = bitmapInUse.findFreeRun(pages);
    if (sp32 < 0) return nullptr;

    const auto sp  = static_cast<uint16_t>(sp32);
    const uint32_t seq = sequenceCounter++;

    /* Пометки в битовых картах */
    bitmapInUse.setRange(sp, pages);
    bitmapAllocated.setRange(sp, pages);

    /* Заполняем мета-запись для стартовой страницы */
    AllocPageMeta& meta = metaTable[sp];
    meta.requestedSize = static_cast<uint32_t>(requestedSize);
    meta.pageCount     = pages;
    meta.startPage     = sp;
    meta.sequenceNum   = seq;

    /* Хедер-канарейка */
    uint8_t* headerAddr = pageAddress(sp);
    BlockGuard::writeHeader(headerAddr);

    /* Футер-канарейка */
    void* footerAddr = BlockGuard::footerFromPage(headerAddr, meta.requestedSize);
    BlockGuard::writeFooter(footerAddr);

    /* Паддинг */
    const size_t padLen = BlockGuard::paddingSize(meta.requestedSize, pages);
    if (padLen > 0U) {
        void* padAddr = BlockGuard::paddingFromPage(headerAddr, meta.requestedSize);
        BlockGuard::fillPadding(padAddr, padLen);
    }

    /* Статистика */
    freePagesCount -= pages;
    if (freePagesCount < minEverFreePages) {
        minEverFreePages = freePagesCount;
    }
    ++successfulAllocs;

    return BlockGuard::userDataFromPage(headerAddr);
}

/* ───────── Деаллокация ───────── */

void PageAllocator::deallocate(void* userPtr) {
    if (!initialized || userPtr == nullptr) return;

    /* Определяем стартовую страницу */
    void* pageStart = BlockGuard::pageFromUserData(userPtr);
    const int32_t pi = pageIndex(pageStart);
    if (pi < 0) {
        ALLOC_DIAG("DEALLOC ERR: ptr=%p вне зоны Z%u [%p..+%u стр]",
                    userPtr, zoneIndex, baseAddress, totalPages);
        ALLOC_FAIL("dealloc: pointer outside zone");
    }
    const auto sp = static_cast<uint16_t>(pi);

    /* Валидация канарейки хедера */
    if (!BlockGuard::validateHeader(pageStart)) {
        ALLOC_DIAG("DEALLOC ERR: header corrupted ptr=%p Z%u page=%u",
                    userPtr, zoneIndex, sp);
        ALLOC_FAIL("dealloc: header canary corrupted");
    }

    /* Получаем метаданные */
    const AllocPageMeta& meta = metaTable[sp];
    if (meta.startPage != sp) {
        ALLOC_DIAG("DEALLOC ERR: meta.startPage=%u != sp=%u, ptr=%p Z%u",
                    meta.startPage, sp, userPtr, zoneIndex);
        ALLOC_FAIL("dealloc: meta startPage mismatch");
    }
    if (meta.pageCount == 0U) {
        ALLOC_DIAG("DEALLOC ERR: meta.pageCount=0, ptr=%p Z%u page=%u seq=%u",
                    userPtr, zoneIndex, sp, meta.sequenceNum);
        ALLOC_FAIL("dealloc: meta pageCount is zero");
    }

    /* Валидация канарейки футера */
    const void* footerAddr = BlockGuard::footerFromPage(pageStart, meta.requestedSize);
    if (!BlockGuard::validateFooter(footerAddr)) {
        ALLOC_DIAG("DEALLOC ERR: footer corrupted ptr=%p Z%u page=%u size=%u seq=%u",
                    userPtr, zoneIndex, sp, meta.requestedSize, meta.sequenceNum);
        ALLOC_FAIL("dealloc: footer canary corrupted");
    }

    /* Принадлежность зоне */
    if (static_cast<uint32_t>(sp) + meta.pageCount > totalPages) {
        ALLOC_DIAG("DEALLOC ERR: block [page=%u +%u стр] выходит за зону Z%u (%u стр), ptr=%p",
                    sp, meta.pageCount, zoneIndex, totalPages, userPtr);
        ALLOC_FAIL("dealloc: block extends beyond zone");
    }

    /* Проверки целостности */
#if ALLOC_QUARANTINE_CHECK_LEVEL > 0
    if (!verifyQuarantine()) { ALLOC_FAIL("quarantine corrupted on dealloc"); }
#endif
#if ALLOC_CHECK_ALL_ALLOCATED
    if (!verifyAllocated()) { ALLOC_FAIL("allocated blocks corrupted on dealloc"); }
#endif

    /* Добавление в карантин (с возможным вытеснением) */
    AllocQuarantineEntry evicted{};
    const bool didEvict = quarantine.add(sp, meta.pageCount, meta.requestedSize,
                                         zoneIndex, &evicted);
    if (didEvict) {
        evictFromQuarantine(evicted);
    }

    /* Заполнение payload карантинным паттерном */
#if ALLOC_FILL_ON_FREE
    BlockGuard::fillQuarantinePayload(userPtr, meta.requestedSize);
#endif

    /* Обновление битовых карт:
     *   bitmapInUse      — остаётся 1 (карантин = «занято» для аллокатора)
     *   bitmapAllocated  — сбрасывается в 0 (уже не живая аллокация)
     */
    bitmapAllocated.clearRange(sp, meta.pageCount);

    /* MPU-защита карантинных страниц */
#if ALLOC_ENABLE_MPU_PROTECTION
    updateMpuProtection(sp, meta.pageCount);
#endif

    ++successfulFrees;
}

/* ───────── Calloc ───────── */

void* PageAllocator::calloc(size_t num, size_t elemSize) {
    if (num > 0U && elemSize > SIZE_MAX / num) return nullptr;
    const size_t total = num * elemSize;
    void* ptr = allocate(total);
    if (ptr != nullptr) {
        std::memset(ptr, 0, total);
    }
    return ptr;
}

/* ───────── Вытеснение из карантина ───────── */

void PageAllocator::evictFromQuarantine(const AllocQuarantineEntry& entry) {
    /* Снять MPU */
    if (entry.mpuRegion >= 0) {
        MpuGuard::unprotect(entry.mpuRegion);
    }

    /* Очистка страниц (если включена) */
#if ALLOC_ENABLE_CLEAR_ON_EVICT
    uint8_t* start = pageAddress(entry.startPage);
    const size_t bytes = static_cast<size_t>(entry.pageCount) * ALLOC_PAGE_SIZE;
    BlockGuard::fillClearedPages(start, bytes);
#endif

    /* Очистка мета-записи стартовой страницы */
    std::memset(&metaTable[entry.startPage], 0, sizeof(AllocPageMeta));

    /* Освобождение в битовых картах */
    bitmapInUse.clearRange(entry.startPage, entry.pageCount);
    /* bitmapAllocated уже 0 для карантинных записей */

    freePagesCount += entry.pageCount;
}

/* ───────── MPU ───────── */

void PageAllocator::updateMpuProtection(uint16_t startPage, uint16_t pageCount) {
    if (!MpuGuard::available()) return;

    /* Расширяем диапазон влево/вправо пока страницы не «allocated» */
    uint16_t regionStart = startPage;
    uint16_t regionEnd   = static_cast<uint16_t>(startPage + pageCount);

    while (regionStart > 0U && !bitmapAllocated.test(static_cast<uint16_t>(regionStart - 1U))) {
        --regionStart;
    }
    while (regionEnd < totalPages && !bitmapAllocated.test(regionEnd)) {
        ++regionEnd;
    }

    const uint16_t regionPages = static_cast<uint16_t>(regionEnd - regionStart);
    const size_t   regionBytes = static_cast<size_t>(regionPages) * ALLOC_PAGE_SIZE;
    const uintptr_t regionAddr = reinterpret_cast<uintptr_t>(pageAddress(regionStart));

    /* Наибольшая степень двойки, помещающаяся и выровненная */
    size_t protectSize   = MpuGuard::floorPow2(regionBytes);
    uintptr_t protectAddr = MpuGuard::alignDown(regionAddr, protectSize);

    while (protectSize > ALLOC_PAGE_SIZE) {
        const uintptr_t endAddr = protectAddr + protectSize;
        const bool ok = (protectAddr >= reinterpret_cast<uintptr_t>(pageAddress(regionStart)))
                     && (endAddr     <= reinterpret_cast<uintptr_t>(pageAddress(regionEnd)));
        if (ok) break;
        protectSize /= 2U;
        protectAddr = MpuGuard::alignDown(
            reinterpret_cast<uintptr_t>(pageAddress(startPage)), protectSize);
    }

    /* Снимаем старые MPU-регионы, покрываемые новым */
    for (uint16_t i = 0; i < QuarantineTable::capacity(); ++i) {
        auto* e = quarantine.entryAt(i);
        if (!e->active || e->mpuRegion < 0) continue;

        const uintptr_t ea = reinterpret_cast<uintptr_t>(pageAddress(e->startPage));
        const uintptr_t ee = ea + static_cast<size_t>(e->pageCount) * ALLOC_PAGE_SIZE;
        if (ea >= protectAddr && ee <= protectAddr + protectSize) {
            MpuGuard::unprotect(e->mpuRegion);
            e->mpuRegion = -1;
        }
    }

    /* Защищаем объединённый регион и обновляем записи */
    const int region = MpuGuard::protect(protectAddr, protectSize);
    if (region >= 0) {
        for (uint16_t i = 0; i < QuarantineTable::capacity(); ++i) {
            auto* e = quarantine.entryAt(i);
            if (!e->active) continue;

            const uintptr_t ea = reinterpret_cast<uintptr_t>(pageAddress(e->startPage));
            const uintptr_t ee = ea + static_cast<size_t>(e->pageCount) * ALLOC_PAGE_SIZE;
            if (ea >= protectAddr && ee <= protectAddr + protectSize) {
                e->mpuRegion = static_cast<int8_t>(region);
            }
        }
    }
}

/* ───────── Информация ───────── */

size_t PageAllocator::freeBytes()        const { return initialized ? freePagesCount   * ALLOC_PAGE_SIZE : 0U; }
size_t PageAllocator::minEverFreeBytes() const { return initialized ? minEverFreePages * ALLOC_PAGE_SIZE : 0U; }
size_t PageAllocator::totalBytes()       const { return initialized ? static_cast<size_t>(totalPages) * ALLOC_PAGE_SIZE : 0U; }
size_t PageAllocator::usedBytes()        const { return totalBytes() - freeBytes(); }
bool   PageAllocator::isInitialized()    const { return initialized; }

bool PageAllocator::ownsPointer(const void* userPtr) const {
    if (!initialized || userPtr == nullptr) return false;
    const auto* ptr = static_cast<const uint8_t*>(userPtr);
    const uint8_t* lo = baseAddress + ALLOC_HEADER_SIZE;
    const uint8_t* hi = baseAddress + static_cast<size_t>(totalPages) * ALLOC_PAGE_SIZE;
    return ptr >= lo && ptr < hi;
}

/* ───────── Верификация карантина ───────── */

bool PageAllocator::verifyQuarantine() const {
    for (uint16_t i = 0; i < QuarantineTable::capacity(); ++i) {
        const auto* entry = quarantine.entryAt(i);
        if (!entry->active) continue;

        const void* pg = pageAddress(entry->startPage);
        const void* userData = BlockGuard::userDataFromPage(pg);

        /* Проверяем канарейку хедера */
        if (!BlockGuard::validateHeader(pg)) {
            ALLOC_DIAG("QUARANTINE: header corrupted Z%u qslot=%u page=%u addr=%p size=%u",
                        zoneIndex, i, entry->startPage, userData, entry->requestedSize);
            return false;
        }

        /* Проверяем канарейку футера (используем requestedSize из карантинной записи) */
        const void* footer = BlockGuard::footerFromPage(pg, entry->requestedSize);
        if (!BlockGuard::validateFooter(footer)) {
            ALLOC_DIAG("QUARANTINE: footer corrupted Z%u qslot=%u page=%u addr=%p size=%u",
                        zoneIndex, i, entry->startPage, userData, entry->requestedSize);
            return false;
        }

#if ALLOC_QUARANTINE_CHECK_LEVEL >= 2
        const void* payload = BlockGuard::userDataFromPage(pg);
        if (!BlockGuard::validateQuarantinePayload(payload, entry->requestedSize)) {
            ALLOC_DIAG("QUARANTINE: payload corrupted Z%u qslot=%u page=%u addr=%p size=%u",
                        zoneIndex, i, entry->startPage, userData, entry->requestedSize);
            return false;
        }
#endif

#if ALLOC_QUARANTINE_CHECK_LEVEL >= 3
        const void* pad = BlockGuard::paddingFromPage(pg, entry->requestedSize);
        const size_t ps = BlockGuard::paddingSize(entry->requestedSize, entry->pageCount);
        if (ps > 0U && !BlockGuard::validatePadding(pad, ps)) {
            ALLOC_DIAG("QUARANTINE: padding corrupted Z%u qslot=%u page=%u addr=%p size=%u",
                        zoneIndex, i, entry->startPage, userData, entry->requestedSize);
            return false;
        }
#endif
    }
    return true;
}

/* ───────── Верификация аллоцированных областей ───────── */

bool PageAllocator::verifyAllocated() const {
    for (uint16_t i = 0; i < totalPages; ) {
        if (!bitmapAllocated.test(i)) {
            ++i;
            continue;
        }

        /* Проверяем, является ли страница стартовой (через мета-таблицу) */
        const AllocPageMeta& meta = metaTable[i];
        if (meta.startPage != i || meta.pageCount == 0U) {
            /* Продолжение многостраничной аллокации — пропускаем */
            ++i;
            continue;
        }

        const void* pg = pageAddress(i);
        const void* userData = BlockGuard::userDataFromPage(pg);

        /* Валидация канарейки хедера */
        if (!BlockGuard::validateHeader(pg)) {
            ALLOC_DIAG("ALLOCATED: header corrupted Z%u page=%u addr=%p size=%u seq=%u",
                        zoneIndex, i, userData, meta.requestedSize, meta.sequenceNum);
            return false;
        }

        /* Валидация канарейки футера */
        const void* footer = BlockGuard::footerFromPage(pg, meta.requestedSize);
        if (!BlockGuard::validateFooter(footer)) {
            ALLOC_DIAG("ALLOCATED: footer corrupted Z%u page=%u addr=%p size=%u seq=%u",
                        zoneIndex, i, userData, meta.requestedSize, meta.sequenceNum);
            return false;
        }

        i = static_cast<uint16_t>(i + meta.pageCount);
    }
    return true;
}

/* ───────── Запуск всех проверок ───────── */

bool PageAllocator::runChecks() const {
    bool ok = true;
#if ALLOC_QUARANTINE_CHECK_LEVEL > 0
    ok = ok && verifyQuarantine();
#endif
#if ALLOC_CHECK_ALL_ALLOCATED
    ok = ok && verifyAllocated();
#endif
    return ok;
}

} // namespace AllocCustom
