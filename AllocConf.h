/**
 * @file AllocConf.h
 * @brief Конфигурация страничного аллокатора.
 *
 * Все параметры переопределяются через -D флаги компилятора.
 */
#pragma once

/* ──────────── Утилиты ──────────── */

#ifdef __cplusplus
    #define ALLOC_STATIC_ASSERT(expr, msg) static_assert(expr, msg)
#else
    #define ALLOC_STATIC_ASSERT(expr, msg) _Static_assert(expr, msg)
#endif

#ifndef ALLOC_ASSERT
    #ifdef __cplusplus
        #include <cassert>
    #else
        #include <assert.h>
    #endif
    #define ALLOC_ASSERT(expr) assert(expr)
#endif

/* ──────────── Геометрия страницы ──────────── */

/** Размер физической страницы (байт). Все аллокации кратны этому значению. */
#ifndef ALLOC_PAGE_SIZE
#define ALLOC_PAGE_SIZE 1024U
#endif

/** Размер хедера области (байт). */
#ifndef ALLOC_HEADER_SIZE
#define ALLOC_HEADER_SIZE 32U
#endif

/** Размер футера области (байт). */
#ifndef ALLOC_FOOTER_SIZE
#define ALLOC_FOOTER_SIZE 32U
#endif

/* ──────────── Лимиты ──────────── */

/** Максимальное число зон памяти. */
#ifndef ALLOC_MAX_ZONES
#define ALLOC_MAX_ZONES 2U
#endif

/** Максимальное число страниц на одну зону (10 МиБ / 1024). */
#ifndef ALLOC_MAX_PAGES_PER_ZONE
#define ALLOC_MAX_PAGES_PER_ZONE 10240U
#endif

/** Размер таблицы карантина (записей о последних освобождениях). */
#ifndef ALLOC_QUARANTINE_CAPACITY
#define ALLOC_QUARANTINE_CAPACITY 32U
#endif

/* ──────────── Паттерны ──────────── */

/** Магическое число хедера. */
#ifndef ALLOC_PATTERN_HEADER_MAGIC
#define ALLOC_PATTERN_HEADER_MAGIC 0x48454144U  /* "HEAD" */
#endif

/** Магическое число футера. */
#ifndef ALLOC_PATTERN_FOOTER_MAGIC
#define ALLOC_PATTERN_FOOTER_MAGIC 0x464F4F54U  /* "FOOT" */
#endif

/** Байт-паттерн заполнения паддинга. */
#ifndef ALLOC_PATTERN_PADDING
#define ALLOC_PATTERN_PADDING 0xFEU
#endif

/** Байт-паттерн заполнения payload при отправке в карантин. */
#ifndef ALLOC_PATTERN_QUARANTINE_FILL
#define ALLOC_PATTERN_QUARANTINE_FILL 0xCDU
#endif

/** Байт-паттерн очищенной страницы (при освобождении из карантина). */
#ifndef ALLOC_PATTERN_CLEARED_PAGE
#define ALLOC_PATTERN_CLEARED_PAGE 0x00U
#endif

/* ──────────── Функциональность ──────────── */

/** Заполнять payload карантинным паттерном при free. */
#ifndef ALLOC_FILL_ON_FREE
#define ALLOC_FILL_ON_FREE 1
#endif

/** Очищать страницы при вытеснении из карантина. */
#ifndef ALLOC_ENABLE_CLEAR_ON_EVICT
#define ALLOC_ENABLE_CLEAR_ON_EVICT 1
#endif

/**
 * Уровень проверки карантина при каждой alloc/free.
 *   0 — отключено
 *   1 — только хедер + футер
 *   2 — + payload
 *   3 — + паддинг
 */
#ifndef ALLOC_QUARANTINE_CHECK_LEVEL
#define ALLOC_QUARANTINE_CHECK_LEVEL 1
#endif

/** Проверять хедеры и футеры ВСЕХ аллоцированных областей при alloc/free. */
#ifndef ALLOC_CHECK_ALL_ALLOCATED
#define ALLOC_CHECK_ALL_ALLOCATED 0
#endif

/** Защита карантинных страниц через MPU. */
#ifndef ALLOC_ENABLE_MPU_PROTECTION
#define ALLOC_ENABLE_MPU_PROTECTION 0
#endif

/** Первый регион MPU, доступный аллокатору. */
#ifndef ALLOC_MPU_FIRST_REGION
#define ALLOC_MPU_FIRST_REGION 4
#endif

/** Число регионов MPU, доступных аллокатору. */
#ifndef ALLOC_MPU_REGION_COUNT
#define ALLOC_MPU_REGION_COUNT 2
#endif
