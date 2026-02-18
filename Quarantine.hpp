/**
 * @file Quarantine.hpp
 * @brief Таблица карантинных областей (POD, trivially constructible).
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include "AllocConf.h"
#include "AllocTypes.h"

namespace AllocCustom {

/**
 * @brief Таблица карантина для одной зоны.
 *
 * Хранит последние ALLOC_QUARANTINE_CAPACITY освобождённых областей.
 * При заполнении вытесняется самая старая запись (FIFO).
 * POD-тип: zero-init из BSS безопасен.
 */
struct QuarantineTable {
    AllocQuarantineEntry entries[ALLOC_QUARANTINE_CAPACITY];
    uint32_t nextSequence;    /**< Следующий порядковый номер free */
    uint16_t activeCount;     /**< Число активных записей */

    /** Инициализация: обнуление всех записей. */
    void init();

    /**
     * Добавить область в карантин.
     * Если таблица полна, вытесняет самую старую запись.
     * @param evicted [out] вытесненная запись (если произошло вытеснение).
     * @return true если произошло вытеснение.
     */
    bool add(uint16_t startPage, uint16_t pageCount,
             uint32_t requestedSize, uint8_t zoneIndex,
             AllocQuarantineEntry* evicted);

    /** Найти самую старую активную запись. */
    AllocQuarantineEntry* findOldest();

    /** Деактивировать запись. */
    void deactivate(AllocQuarantineEntry* entry);

    bool     isEmpty() const;
    bool     isFull()  const;
    uint16_t count()   const;

    const AllocQuarantineEntry* entryAt(uint16_t idx) const;
    AllocQuarantineEntry*       entryAt(uint16_t idx);

    static constexpr uint16_t capacity() { return ALLOC_QUARANTINE_CAPACITY; }
};

} // namespace AllocCustom
