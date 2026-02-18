/**
 * @file Quarantine.cpp
 * @brief Реализация таблицы карантина.
 */
#include "Quarantine.hpp"
#include <cstring>
#include <climits>

namespace AllocCustom {

void QuarantineTable::init() {
    std::memset(entries, 0, sizeof(entries));
    nextSequence = 1U;   /* 0 = неиспользованная запись */
    activeCount  = 0U;
}

bool QuarantineTable::add(uint16_t startPage, uint16_t pageCount,
                           uint32_t requestedSize, uint8_t zoneIndex,
                           AllocQuarantineEntry* evicted) {
    bool didEvict = false;

    if (isFull()) {
        AllocQuarantineEntry* oldest = findOldest();
        ALLOC_ASSERT(oldest != nullptr);
        if (evicted != nullptr) {
            *evicted = *oldest;
        }
        oldest->active = 0U;
        --activeCount;
        didEvict = true;
    }

    /* Поиск свободного слота */
    AllocQuarantineEntry* slot = nullptr;
    for (uint16_t i = 0; i < ALLOC_QUARANTINE_CAPACITY; ++i) {
        if (!entries[i].active) {
            slot = &entries[i];
            break;
        }
    }
    ALLOC_ASSERT(slot != nullptr);

    slot->startPage     = startPage;
    slot->pageCount     = pageCount;
    slot->requestedSize = requestedSize;
    slot->freeSequence   = nextSequence++;
    slot->mpuRegion     = -1;
    slot->zoneIndex     = zoneIndex;
    slot->active        = 1U;
    slot->reserved      = 0U;
    ++activeCount;

    return didEvict;
}

AllocQuarantineEntry* QuarantineTable::findOldest() {
    AllocQuarantineEntry* oldest = nullptr;
    uint32_t minSeq = UINT32_MAX;

    for (uint16_t i = 0; i < ALLOC_QUARANTINE_CAPACITY; ++i) {
        if (entries[i].active && entries[i].freeSequence < minSeq) {
            minSeq  = entries[i].freeSequence;
            oldest  = &entries[i];
        }
    }
    return oldest;
}

void QuarantineTable::deactivate(AllocQuarantineEntry* entry) {
    ALLOC_ASSERT(entry != nullptr);
    ALLOC_ASSERT(entry->active);
    entry->active = 0U;
    --activeCount;
}

bool     QuarantineTable::isEmpty() const { return activeCount == 0U; }
bool     QuarantineTable::isFull()  const { return activeCount >= ALLOC_QUARANTINE_CAPACITY; }
uint16_t QuarantineTable::count()   const { return activeCount; }

const AllocQuarantineEntry* QuarantineTable::entryAt(uint16_t idx) const {
    ALLOC_ASSERT(idx < ALLOC_QUARANTINE_CAPACITY);
    return &entries[idx];
}

AllocQuarantineEntry* QuarantineTable::entryAt(uint16_t idx) {
    ALLOC_ASSERT(idx < ALLOC_QUARANTINE_CAPACITY);
    return &entries[idx];
}

} // namespace AllocCustom
