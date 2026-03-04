/**
 * @file BlockGuard.cpp
 * @brief Реализация канареек и навигации по областям.
 *
 * Хедер/футер — чистые канарейки (повторяющийся 4-байтный паттерн).
 * Никаких структурированных метаданных в области выделения.
 */
#include "BlockGuard.hpp"
#include "AllocConf.h"
#include <cstring>
#ifndef HOST_BUILD
  /* Data Synchronization Barrier — сброс буфера записи */
  #ifndef __DSB
    #define __DSB() __asm volatile("dsb 0xF" ::: "memory") // NOLINT(bugprone-reserved-identifier)
  #endif
#endif

namespace AllocCustom {

/* ───────── Канарейки ───────── */

void BlockGuard::writeCanary(void* dest, uint32_t pattern, size_t size) {
    auto* p   = static_cast<uint8_t*>(dest);
    const auto* pat = reinterpret_cast<const uint8_t*>(&pattern);
    for (size_t i = 0; i < size; ++i) {
        p[i] = pat[i % 4U];
    }
}

bool BlockGuard::validateCanary(const void* src, uint32_t pattern, size_t size) {
    const auto* p   = static_cast<const uint8_t*>(src);
    const auto* pat = reinterpret_cast<const uint8_t*>(&pattern);
    for (size_t i = 0; i < size; ++i) {
        if (p[i] != pat[i % 4U]) return false;
    }
    return true;
}

void BlockGuard::writeHeader(void* dest) {
    writeCanary(dest, ALLOC_PATTERN_HEADER_MAGIC, ALLOC_HEADER_SIZE);
}

void BlockGuard::writeFooter(void* dest) {
    writeCanary(dest, ALLOC_PATTERN_FOOTER_MAGIC, ALLOC_FOOTER_SIZE);
}

bool BlockGuard::validateHeader(const void* headerPtr) {
    return validateCanary(headerPtr, ALLOC_PATTERN_HEADER_MAGIC, ALLOC_HEADER_SIZE);
}

bool BlockGuard::validateFooter(const void* footerPtr) {
    return validateCanary(footerPtr, ALLOC_PATTERN_FOOTER_MAGIC, ALLOC_FOOTER_SIZE);
}

/* ───────── Паттерны ───────── */

void BlockGuard::fillPadding(void* paddingStart, size_t size) {
    std::memset(paddingStart, ALLOC_PATTERN_PADDING, size);
}

void BlockGuard::fillQuarantinePayload(void* payloadStart, size_t size) {
    std::memset(payloadStart, ALLOC_PATTERN_QUARANTINE_FILL, size);
#ifndef HOST_BUILD
    __DSB();  /* Гарантируем сброс буфера записи — критично для PSRAM через QSPI */
#endif
}

void BlockGuard::fillClearedPages(void* start, size_t size) {
    std::memset(start, ALLOC_PATTERN_CLEARED_PAGE, size);
}

bool BlockGuard::validatePadding(const void* paddingStart, size_t size) {
    const auto* p = static_cast<const uint8_t*>(paddingStart);
    for (size_t i = 0; i < size; ++i) {
        if (p[i] != ALLOC_PATTERN_PADDING) return false;
    }
    return true;
}

bool BlockGuard::validateQuarantinePayload(const void* start, size_t size) {
    const auto* p = static_cast<const uint8_t*>(start);
    for (size_t i = 0; i < size; ++i) {
        if (p[i] != ALLOC_PATTERN_QUARANTINE_FILL) return false;
    }
    return true;
}

/* ───────── Навигация ───────── */

void* BlockGuard::userDataFromPage(void* pageStart) {
    return static_cast<uint8_t*>(pageStart) + ALLOC_HEADER_SIZE;
}

const void* BlockGuard::userDataFromPage(const void* pageStart) {
    return static_cast<const uint8_t*>(pageStart) + ALLOC_HEADER_SIZE;
}

void* BlockGuard::pageFromUserData(void* userData) {
    return static_cast<uint8_t*>(userData) - ALLOC_HEADER_SIZE;
}

const void* BlockGuard::pageFromUserData(const void* userData) {
    return static_cast<const uint8_t*>(userData) - ALLOC_HEADER_SIZE;
}

void* BlockGuard::footerFromPage(void* pageStart, uint32_t requestedSize) {
    return static_cast<uint8_t*>(pageStart) + ALLOC_HEADER_SIZE + requestedSize;
}

const void* BlockGuard::footerFromPage(const void* pageStart, uint32_t requestedSize) {
    return static_cast<const uint8_t*>(pageStart) + ALLOC_HEADER_SIZE + requestedSize;
}

void* BlockGuard::paddingFromPage(void* pageStart, uint32_t requestedSize) {
    return static_cast<uint8_t*>(pageStart) +
           ALLOC_HEADER_SIZE + requestedSize + ALLOC_FOOTER_SIZE;
}

const void* BlockGuard::paddingFromPage(const void* pageStart, uint32_t requestedSize) {
    return static_cast<const uint8_t*>(pageStart) +
           ALLOC_HEADER_SIZE + requestedSize + ALLOC_FOOTER_SIZE;
}

size_t BlockGuard::paddingSize(uint32_t requestedSize, uint16_t pageCount) {
    const size_t totalBytes = static_cast<size_t>(pageCount) * ALLOC_PAGE_SIZE;
    const size_t usedBytes  = ALLOC_HEADER_SIZE + requestedSize + ALLOC_FOOTER_SIZE;
    ALLOC_ASSERT(totalBytes >= usedBytes);
    return totalBytes - usedBytes;
}

} // namespace AllocCustom
