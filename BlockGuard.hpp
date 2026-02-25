/**
 * @file BlockGuard.hpp
 * @brief Создание и валидация канареек (хедер/футер) и паттернов.
 *
 * Канарейки — повторяющийся 4-байтный паттерн, проверяемый побайтово.
 * Метаданные аллокации хранятся отдельно (AllocPageMeta), не в области данных.
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include "AllocConf.h"

namespace AllocCustom {

/**
 * @brief Статические методы для работы с канарейками и паттернами.
 */
struct BlockGuard {
    /* ── Канарейки ── */

    /** Заполнить dest повторяющимся 4-байтным паттерном. */
    static void writeCanary(void* dest, uint32_t pattern, size_t size);

    /** Проверить что src заполнен повторяющимся паттерном. */
    static bool validateCanary(const void* src, uint32_t pattern, size_t size);

    /** Записать хедер-канарейку (ALLOC_HEADER_SIZE байт). */
    static void writeHeader(void* dest);

    /** Записать футер-канарейку (ALLOC_FOOTER_SIZE байт). */
    static void writeFooter(void* dest);

    /** Валидировать хедер-канарейку. */
    static bool validateHeader(const void* headerPtr);

    /** Валидировать футер-канарейку. */
    static bool validateFooter(const void* footerPtr);

    /* ── Паттерны ── */

    static void fillPadding(void* paddingStart, size_t size);
    static void fillQuarantinePayload(void* payloadStart, size_t size);
    static void fillClearedPages(void* start, size_t size);

    static bool validatePadding(const void* paddingStart, size_t size);
    static bool validateQuarantinePayload(const void* start, size_t size);

    /* ── Навигация по области ── */

    /** Указатель на пользовательские данные из начала страницы (хедера). */
    static void*       userDataFromPage(void* pageStart);
    static const void* userDataFromPage(const void* pageStart);

    /** Начало страницы (хедера) из указателя на пользовательские данные. */
    static void*       pageFromUserData(void* userData);
    static const void* pageFromUserData(const void* userData);

    /** Указатель на футер по началу страницы и requestedSize (из мета-таблицы). */
    static void*       footerFromPage(void* pageStart, uint32_t requestedSize);
    static const void* footerFromPage(const void* pageStart, uint32_t requestedSize);

    /** Указатель на паддинг по началу страницы, requestedSize и pageCount. */
    static void*       paddingFromPage(void* pageStart, uint32_t requestedSize);
    static const void* paddingFromPage(const void* pageStart, uint32_t requestedSize);

    /** Размер паддинга. */
    static size_t paddingSize(uint32_t requestedSize, uint16_t pageCount);
};

} // namespace AllocCustom
