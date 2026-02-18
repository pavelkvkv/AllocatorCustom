/**
 * @file PageBitmap.cpp
 * @brief Реализация битовой карты страниц.
 */
#include "PageBitmap.hpp"
#include <cstring>

namespace AllocCustom {

void PageBitmap::init(uint16_t count) {
    ALLOC_ASSERT(count <= ALLOC_MAX_PAGES_PER_ZONE);
    std::memset(words, 0, sizeof(words));
    pageCount = count;
}

void PageBitmap::set(uint16_t page) {
    ALLOC_ASSERT(page < pageCount);
    words[page / 32U] |= (1U << (page % 32U));
}

void PageBitmap::clear(uint16_t page) {
    ALLOC_ASSERT(page < pageCount);
    words[page / 32U] &= ~(1U << (page % 32U));
}

bool PageBitmap::test(uint16_t page) const {
    ALLOC_ASSERT(page < pageCount);
    return (words[page / 32U] & (1U << (page % 32U))) != 0U;
}

void PageBitmap::setRange(uint16_t start, uint16_t count) {
    ALLOC_ASSERT(start + count <= pageCount);
    for (uint16_t i = 0; i < count; ++i) {
        set(static_cast<uint16_t>(start + i));
    }
}

void PageBitmap::clearRange(uint16_t start, uint16_t count) {
    ALLOC_ASSERT(start + count <= pageCount);
    for (uint16_t i = 0; i < count; ++i) {
        clear(static_cast<uint16_t>(start + i));
    }
}

int32_t PageBitmap::findFreeRun(uint16_t count) const {
    if (count == 0 || count > pageCount) {
        return -1;
    }

    uint16_t runStart = 0;
    uint16_t runLen   = 0;

    for (uint16_t i = 0; i < pageCount; ++i) {
        /* Быстрый пропуск полностью занятых 32-битных слов */
        const uint16_t wordIdx = i / 32U;
        if (runLen == 0 && words[wordIdx] == 0xFFFFFFFFU) {
            i = static_cast<uint16_t>((wordIdx + 1U) * 32U - 1U);
            continue;
        }

        if (!test(i)) {
            if (runLen == 0) {
                runStart = i;
            }
            ++runLen;
            if (runLen >= count) {
                return static_cast<int32_t>(runStart);
            }
        } else {
            runLen = 0;
        }
    }
    return -1;
}

uint16_t PageBitmap::countSet() const {
    uint16_t n = 0;
    const uint16_t fullWords = pageCount / 32U;
    for (uint16_t w = 0; w < fullWords; ++w) {
        /* __builtin_popcount доступен в GCC и Clang */
        n = static_cast<uint16_t>(n + __builtin_popcount(words[w]));
    }
    /* Остаток */
    for (uint16_t i = fullWords * 32U; i < pageCount; ++i) {
        if (test(i)) {
            ++n;
        }
    }
    return n;
}

uint16_t PageBitmap::countClear() const {
    return static_cast<uint16_t>(pageCount - countSet());
}

} // namespace AllocCustom
