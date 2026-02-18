/**
 * @file BlockGuard.cpp
 * @brief Реализация создания и валидации хедеров/футеров.
 */
#include "BlockGuard.hpp"
#include <cstring>

namespace AllocCustom {

/* ───────── Контрольная сумма ───────── */

uint32_t BlockGuard::computeChecksum(const void* block, size_t blockSize) {
    const auto* w = static_cast<const uint32_t*>(block);
    const size_t wordCount = blockSize / sizeof(uint32_t);
    ALLOC_ASSERT(wordCount >= 2U);

    uint32_t result = 0U;
    for (size_t i = 0U; i + 1U < wordCount; ++i) {
        result ^= w[i];
    }
    return result;
}

/* ───────── Запись ───────── */

void BlockGuard::writeHeader(void* dest, uint32_t requestedSize,
                              uint16_t startPage, uint16_t pageCount,
                              uint8_t zoneIndex, uint32_t sequenceNum) {
    auto* h = static_cast<AllocBlockHeader*>(dest);
    h->magic         = ALLOC_PATTERN_HEADER_MAGIC;
    h->requestedSize = requestedSize;
    h->startPage     = startPage;
    h->pageCount     = pageCount;
    h->zoneIndex     = zoneIndex;
    std::memset(h->reserved, 0, sizeof(h->reserved));
    h->sequenceNum   = sequenceNum;
    h->reserved2     = 0U;
    h->reserved3     = 0U;
    h->checksum      = computeChecksum(h, sizeof(AllocBlockHeader));
}

void BlockGuard::writeFooter(void* dest, uint32_t requestedSize,
                              uint16_t startPage, uint16_t pageCount,
                              uint8_t zoneIndex, uint32_t sequenceNum) {
    auto* f = static_cast<AllocBlockFooter*>(dest);
    f->magic         = ALLOC_PATTERN_FOOTER_MAGIC;
    f->requestedSize = requestedSize;
    f->startPage     = startPage;
    f->pageCount     = pageCount;
    f->zoneIndex     = zoneIndex;
    std::memset(f->reserved, 0, sizeof(f->reserved));
    f->sequenceNum   = sequenceNum;
    f->reserved2     = 0U;
    f->reserved3     = 0U;
    f->checksum      = computeChecksum(f, sizeof(AllocBlockFooter));
}

/* ───────── Валидация ───────── */

bool BlockGuard::validateHeader(const void* headerPtr) {
    const auto* h = static_cast<const AllocBlockHeader*>(headerPtr);
    if (h->magic != ALLOC_PATTERN_HEADER_MAGIC) return false;
    return h->checksum == computeChecksum(h, sizeof(AllocBlockHeader));
}

bool BlockGuard::validateFooter(const void* footerPtr) {
    const auto* f = static_cast<const AllocBlockFooter*>(footerPtr);
    if (f->magic != ALLOC_PATTERN_FOOTER_MAGIC) return false;
    return f->checksum == computeChecksum(f, sizeof(AllocBlockFooter));
}

bool BlockGuard::validatePair(const AllocBlockHeader* header,
                               const AllocBlockFooter* footer) {
    if (header->requestedSize != footer->requestedSize) return false;
    if (header->startPage     != footer->startPage)     return false;
    if (header->pageCount     != footer->pageCount)     return false;
    if (header->zoneIndex     != footer->zoneIndex)     return false;
    if (header->sequenceNum   != footer->sequenceNum)   return false;
    return true;
}

/* ───────── Паттерны ───────── */

void BlockGuard::fillPadding(void* paddingStart, size_t size) {
    std::memset(paddingStart, ALLOC_PATTERN_PADDING, size);
}

void BlockGuard::fillQuarantinePayload(void* payloadStart, size_t size) {
    std::memset(payloadStart, ALLOC_PATTERN_QUARANTINE_FILL, size);
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

void* BlockGuard::userDataFromHeader(void* headerPtr) {
    return static_cast<uint8_t*>(headerPtr) + ALLOC_HEADER_SIZE;
}

const void* BlockGuard::userDataFromHeader(const void* headerPtr) {
    return static_cast<const uint8_t*>(headerPtr) + ALLOC_HEADER_SIZE;
}

AllocBlockHeader* BlockGuard::headerFromUserData(void* userData) {
    return reinterpret_cast<AllocBlockHeader*>(
        static_cast<uint8_t*>(userData) - ALLOC_HEADER_SIZE);
}

const AllocBlockHeader* BlockGuard::headerFromUserData(const void* userData) {
    return reinterpret_cast<const AllocBlockHeader*>(
        static_cast<const uint8_t*>(userData) - ALLOC_HEADER_SIZE);
}

AllocBlockFooter* BlockGuard::footerFromHeader(AllocBlockHeader* header) {
    return reinterpret_cast<AllocBlockFooter*>(
        reinterpret_cast<uint8_t*>(header) + ALLOC_HEADER_SIZE + header->requestedSize);
}

const AllocBlockFooter* BlockGuard::footerFromHeader(const AllocBlockHeader* header) {
    return reinterpret_cast<const AllocBlockFooter*>(
        reinterpret_cast<const uint8_t*>(header) + ALLOC_HEADER_SIZE + header->requestedSize);
}

void* BlockGuard::paddingFromHeader(AllocBlockHeader* header) {
    return reinterpret_cast<uint8_t*>(header) +
           ALLOC_HEADER_SIZE + header->requestedSize + ALLOC_FOOTER_SIZE;
}

const void* BlockGuard::paddingFromHeader(const AllocBlockHeader* header) {
    return reinterpret_cast<const uint8_t*>(header) +
           ALLOC_HEADER_SIZE + header->requestedSize + ALLOC_FOOTER_SIZE;
}

size_t BlockGuard::paddingSize(const AllocBlockHeader* header) {
    const size_t totalBytes = static_cast<size_t>(header->pageCount) * ALLOC_PAGE_SIZE;
    const size_t usedBytes  = ALLOC_HEADER_SIZE + header->requestedSize + ALLOC_FOOTER_SIZE;
    ALLOC_ASSERT(totalBytes >= usedBytes);
    return totalBytes - usedBytes;
}

} // namespace AllocCustom
