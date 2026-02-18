/**
 * @file PageAllocator.cpp
 * @brief Реализация страничного аллокатора одной зоны.
 */
#include "PageAllocator.hpp"
#include "BlockGuard.hpp"
#include "MpuGuard.hpp"
#include <cstring>

namespace AllocCustom {

/* ───────── Проверки конфигурации ───────── */

static_assert(ALLOC_PAGE_SIZE >= ALLOC_HEADER_SIZE + ALLOC_FOOTER_SIZE + 1U,
              "Страница слишком мала: header + footer + 1 байт payload");
static_assert(sizeof(AllocBlockHeader) == ALLOC_HEADER_SIZE, "Header size");
static_assert(sizeof(AllocBlockFooter) == ALLOC_FOOTER_SIZE, "Footer size");

/* ───────── Инициализация ───────── */

void PageAllocator::init(uint8_t* start, size_t size, uint8_t zone) {
    ALLOC_ASSERT(start != nullptr);
    ALLOC_ASSERT(size >= ALLOC_PAGE_SIZE);

    baseAddress = start;
    regionSize  = size;
    totalPages  = static_cast<uint16_t>(size / ALLOC_PAGE_SIZE);
    zoneIndex   = zone;

    ALLOC_ASSERT(totalPages > 0U);
    ALLOC_ASSERT(totalPages <= ALLOC_MAX_PAGES_PER_ZONE);

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

/* ───────── Аллокация ───────── */

void* PageAllocator::allocate(size_t requestedSize) {
    if (!initialized || requestedSize == 0U) return nullptr;

    const uint16_t pages = pagesNeeded(requestedSize);
    if (pages > freePagesCount) return nullptr;

    /* Проверки целостности перед операцией */
#if ALLOC_QUARANTINE_CHECK_LEVEL > 0
    ALLOC_ASSERT(verifyQuarantine());
#endif
#if ALLOC_CHECK_ALL_ALLOCATED
    ALLOC_ASSERT(verifyAllocated());
#endif

    /* Поиск непрерывного свободного участка */
    const int32_t sp32 = bitmapInUse.findFreeRun(pages);
    if (sp32 < 0) return nullptr;

    const auto sp  = static_cast<uint16_t>(sp32);
    const uint32_t seq = sequenceCounter++;

    /* Пометки в битовых картах */
    bitmapInUse.setRange(sp, pages);
    bitmapAllocated.setRange(sp, pages);

    /* Хедер */
    uint8_t* headerAddr = pageAddress(sp);
    BlockGuard::writeHeader(headerAddr,
                            static_cast<uint32_t>(requestedSize),
                            sp, pages, zoneIndex, seq);

    /* Футер */
    auto* header = reinterpret_cast<AllocBlockHeader*>(headerAddr);
    auto* footer = BlockGuard::footerFromHeader(header);
    BlockGuard::writeFooter(footer,
                            static_cast<uint32_t>(requestedSize),
                            sp, pages, zoneIndex, seq);

    /* Паддинг */
    const size_t padLen = BlockGuard::paddingSize(header);
    if (padLen > 0U) {
        BlockGuard::fillPadding(BlockGuard::paddingFromHeader(header), padLen);
    }

    /* Статистика */
    freePagesCount -= pages;
    if (freePagesCount < minEverFreePages) {
        minEverFreePages = freePagesCount;
    }
    ++successfulAllocs;

    return BlockGuard::userDataFromHeader(headerAddr);
}

/* ───────── Деаллокация ───────── */

void PageAllocator::deallocate(void* userPtr) {
    if (!initialized || userPtr == nullptr) return;

    /* Валидация хедера */
    auto* header = BlockGuard::headerFromUserData(userPtr);
    ALLOC_ASSERT(BlockGuard::validateHeader(header));

    /* Валидация футера */
    const auto* footer = BlockGuard::footerFromHeader(
                             const_cast<const AllocBlockHeader*>(header));
    ALLOC_ASSERT(BlockGuard::validateFooter(footer));
    ALLOC_ASSERT(BlockGuard::validatePair(header, footer));

    /* Принадлежность зоне */
    ALLOC_ASSERT(header->zoneIndex == zoneIndex);
    const uint16_t sp = header->startPage;
    const uint16_t pc = header->pageCount;
    ALLOC_ASSERT(static_cast<uint32_t>(sp) + pc <= totalPages);

    /* Проверки целостности */
#if ALLOC_QUARANTINE_CHECK_LEVEL > 0
    ALLOC_ASSERT(verifyQuarantine());
#endif
#if ALLOC_CHECK_ALL_ALLOCATED
    ALLOC_ASSERT(verifyAllocated());
#endif

    /* Добавление в карантин (с возможным вытеснением) */
    AllocQuarantineEntry evicted{};
    const bool didEvict = quarantine.add(sp, pc, header->requestedSize,
                                         zoneIndex, &evicted);
    if (didEvict) {
        evictFromQuarantine(evicted);
    }

    /* Заполнение payload карантинным паттерном */
#if ALLOC_FILL_ON_FREE
    BlockGuard::fillQuarantinePayload(userPtr, header->requestedSize);
#endif

    /* Обновление битовых карт:
     *   bitmapInUse      — остаётся 1 (карантин = «занято» для аллокатора)
     *   bitmapAllocated  — сбрасывается в 0 (уже не живая аллокация)
     */
    bitmapAllocated.clearRange(sp, pc);

    /* MPU-защита карантинных страниц */
#if ALLOC_ENABLE_MPU_PROTECTION
    updateMpuProtection(sp, pc);
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

        const auto* header = reinterpret_cast<const AllocBlockHeader*>(
            pageAddress(entry->startPage));

        if (!BlockGuard::validateHeader(header)) return false;

        const auto* footer = BlockGuard::footerFromHeader(header);
        if (!BlockGuard::validateFooter(footer)) return false;
        if (!BlockGuard::validatePair(header, footer)) return false;

#if ALLOC_QUARANTINE_CHECK_LEVEL >= 2
        const void* payload = BlockGuard::userDataFromHeader(header);
        if (!BlockGuard::validateQuarantinePayload(payload, header->requestedSize)) {
            return false;
        }
#endif

#if ALLOC_QUARANTINE_CHECK_LEVEL >= 3
        const void* pad = BlockGuard::paddingFromHeader(header);
        const size_t ps = BlockGuard::paddingSize(header);
        if (ps > 0U && !BlockGuard::validatePadding(pad, ps)) {
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

        const auto* header = reinterpret_cast<const AllocBlockHeader*>(pageAddress(i));

        /* Проверяем, является ли страница началом области */
        if (!BlockGuard::validateHeader(header) || header->startPage != i) {
            ++i;
            continue;
        }

        const auto* footer = BlockGuard::footerFromHeader(header);
        if (!BlockGuard::validateFooter(footer)) return false;
        if (!BlockGuard::validatePair(header, footer)) return false;

        i = static_cast<uint16_t>(i + header->pageCount);
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
