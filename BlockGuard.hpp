/**
 * @file BlockGuard.hpp
 * @brief Создание и валидация хедеров/футеров областей.
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include "AllocConf.h"
#include "AllocTypes.h"

namespace AllocCustom {

/**
 * @brief Статические методы для работы с хедерами и футерами.
 *
 * Не содержит состояния — все методы статические.
 */
struct BlockGuard {
    /* ── Контрольная сумма ── */

    /** XOR всех uint32_t слов блока, кроме последнего (checksum). */
    static uint32_t computeChecksum(const void* block, size_t blockSize);

    /* ── Запись ── */

    static void writeHeader(void* dest, uint32_t requestedSize,
                            uint16_t startPage, uint16_t pageCount,
                            uint8_t zoneIndex, uint32_t sequenceNum);

    static void writeFooter(void* dest, uint32_t requestedSize,
                            uint16_t startPage, uint16_t pageCount,
                            uint8_t zoneIndex, uint32_t sequenceNum);

    /* ── Валидация ── */

    static bool validateHeader(const void* headerPtr);
    static bool validateFooter(const void* footerPtr);
    static bool validatePair(const AllocBlockHeader* header,
                             const AllocBlockFooter* footer);

    /* ── Паттерны ── */

    static void fillPadding(void* paddingStart, size_t size);
    static void fillQuarantinePayload(void* payloadStart, size_t size);
    static void fillClearedPages(void* start, size_t size);

    static bool validatePadding(const void* paddingStart, size_t size);
    static bool validateQuarantinePayload(const void* start, size_t size);

    /* ── Навигация по области ── */

    static void*                   userDataFromHeader(void* headerPtr);
    static const void*             userDataFromHeader(const void* headerPtr);

    static AllocBlockHeader*       headerFromUserData(void* userData);
    static const AllocBlockHeader* headerFromUserData(const void* userData);

    static AllocBlockFooter*       footerFromHeader(AllocBlockHeader* header);
    static const AllocBlockFooter* footerFromHeader(const AllocBlockHeader* header);

    static void*                   paddingFromHeader(AllocBlockHeader* header);
    static const void*             paddingFromHeader(const AllocBlockHeader* header);

    static size_t paddingSize(const AllocBlockHeader* header);
};

} // namespace AllocCustom
