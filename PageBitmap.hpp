/**
 * @file PageBitmap.hpp
 * @brief Битовая карта страниц (POD, trivially constructible).
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include "AllocConf.h"

namespace AllocCustom {

/**
 * @brief Битовая карта для отслеживания состояния страниц.
 *
 * POD-тип: zero-init из BSS безопасен. Полная инициализация — через init().
 */
struct PageBitmap {
    static constexpr uint16_t kMaxWords = (ALLOC_MAX_PAGES_PER_ZONE + 31U) / 32U;

    uint32_t words[kMaxWords];  /**< Битовый массив */
    uint16_t pageCount;         /**< Фактическое число страниц в зоне */

    /** Инициализация: обнуление всех бит, установка числа страниц. */
    void init(uint16_t count);

    /** Установить бит страницы (пометить как 1). */
    void set(uint16_t page);

    /** Снять бит страницы (пометить как 0). */
    void clear(uint16_t page);

    /** Проверить бит страницы. */
    bool test(uint16_t page) const;

    /** Установить диапазон бит [start, start+count). */
    void setRange(uint16_t start, uint16_t count);

    /** Снять диапазон бит [start, start+count). */
    void clearRange(uint16_t start, uint16_t count);

    /**
     * Найти первый непрерывный участок из count нулевых бит.
     * @return Индекс первой страницы или -1 если не найден.
     */
    int32_t findFreeRun(uint16_t count) const;

    /** Число установленных бит. */
    uint16_t countSet() const;

    /** Число нулевых бит (свободных страниц). */
    uint16_t countClear() const;
};

} // namespace AllocCustom
