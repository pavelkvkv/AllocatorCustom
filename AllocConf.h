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

/**
 * ALLOC_ASSERT — переопределяется извне.
 * На хосте: assert(). На таргете (FreeRTOS): configASSERT().
 */
#ifndef ALLOC_ASSERT
    #if defined(HOST_BUILD)
        #ifdef __cplusplus
            #include <cassert>
        #else
            #include <assert.h>
        #endif
        #define ALLOC_ASSERT(expr) assert(expr)
    #else
        /* На таргете используем configASSERT из FreeRTOS.h */
        #include "FreeRTOS.h"
        #define ALLOC_ASSERT(expr) configASSERT(expr)
    #endif
#endif

/* ──────────── Геометрия страницы ──────────── */

/** Размер физической страницы (байт). Все аллокации кратны этому значению. */
#ifndef ALLOC_PAGE_SIZE
#define ALLOC_PAGE_SIZE 512U
#endif

/**
 * Размер хедера-канарейки (байт). Произвольный, ≥ 1.
 * Заполняется повторяющимся ALLOC_PATTERN_HEADER_MAGIC.
 */
#ifndef ALLOC_HEADER_SIZE
#define ALLOC_HEADER_SIZE 8U
#endif

/**
 * Размер футера-канарейки (байт). Произвольный, ≥ 1.
 * Заполняется повторяющимся ALLOC_PATTERN_FOOTER_MAGIC.
 */
#ifndef ALLOC_FOOTER_SIZE
#define ALLOC_FOOTER_SIZE 8U
#endif

/* ──────────── Лимиты ──────────── */

/** Максимальное число зон памяти. */
#ifndef ALLOC_MAX_ZONES
#define ALLOC_MAX_ZONES 2U
#endif

/** Максимальное число страниц на одну зону (10 МиБ / 1024). */
#ifndef ALLOC_MAX_PAGES_PER_ZONE
#define ALLOC_MAX_PAGES_PER_ZONE (10U * 1024U * 1024U / ALLOC_PAGE_SIZE)
#endif

/** Размер таблицы карантина (записей о последних освобождениях). Карантин per-zone*/
#ifndef ALLOC_QUARANTINE_CAPACITY
#define ALLOC_QUARANTINE_CAPACITY 8U
#endif

/** Длина хранимого имени таска (с '\0'). Используется в карантине и трассировке. */
#ifndef ALLOC_TASK_NAME_LEN
#define ALLOC_TASK_NAME_LEN 12U
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
#define ALLOC_QUARANTINE_CHECK_LEVEL 2
#endif

/** Проверять хедеры и футеры ВСЕХ аллоцированных областей при alloc/free. */
#ifndef ALLOC_CHECK_ALL_ALLOCATED
#define ALLOC_CHECK_ALL_ALLOCATED 1
#endif

/**
 * Проверять карантин и аллоцированные области ВСЕХ зон при каждой операции.
 * Без этого флага каждая зона проверяет только себя — порча в Z1 не будет
 * обнаружена до операции именно в Z1.
 * 0 — отключено (per-zone проверки), 1 — включено (cross-zone).
 */
#ifndef ALLOC_CHECK_CROSS_ZONE
#define ALLOC_CHECK_CROSS_ZONE 1
#endif

/** Защита карантинных страниц через MPU. 
 * Примечание: MPU-защита используется только для быстрого обнаружения UAF на отладке, 
 * но не заменяет полноценную проверку канареек и payload.
*/
#ifndef ALLOC_ENABLE_MPU_PROTECTION
#define ALLOC_ENABLE_MPU_PROTECTION 1
#endif

/** Первый регион MPU, доступный аллокатору. */
#ifndef ALLOC_MPU_FIRST_REGION
#define ALLOC_MPU_FIRST_REGION 4
#endif

/** Число регионов MPU, доступных аллокатору. */
#ifndef ALLOC_MPU_REGION_COUNT
#define ALLOC_MPU_REGION_COUNT 2
#endif

/* ──────────── Логирование ──────────── */

/**
 * Включить логирование через Log-компонент (только на таргете).
 * На хосте (HOST_BUILD) логи всегда отключены.
 */
#ifndef ALLOC_ENABLE_LOGGING
#define ALLOC_ENABLE_LOGGING 1
#endif

/**
 * Включить предупреждение о мелких аллокациях.
 * Работает независимо от ALLOC_LOG_SECONDARY_ZONE_WARN.
 * 0 — отключено, 1 — включено.
 */
#ifndef ALLOC_LOG_SMALL_ALLOC_WARN
#define ALLOC_LOG_SMALL_ALLOC_WARN 1
#endif

/**
 * Порог «маленькой аллокации» для быстрой зоны (zone 0), байт.
 * Мелкие аллокации в SRAM занимают целую страницу — повод для ревью.
 * Аллокации < этого значения вызывают logW с таском-инициатором.
 */
#ifndef ALLOC_LOG_SMALL_ALLOC_THRESHOLD_FAST
#define ALLOC_LOG_SMALL_ALLOC_THRESHOLD_FAST 64U
#endif

/**
 * Порог «маленькой аллокации» для медленной зоны (zone 1), байт.
 * В PSRAM мелкие объекты тормозят больше: рекомендуется выше порога FAST.
 * Аллокации < этого значения вызывают logW с таском-инициатором.
 */
#ifndef ALLOC_LOG_SMALL_ALLOC_THRESHOLD_SLOW
#define ALLOC_LOG_SMALL_ALLOC_THRESHOLD_SLOW 256U
#endif

/**
 * Логировать предупреждение при аллокации во вторичной зоне (fallback).
 * 0 — отключено, 1 — включено.
 */
#ifndef ALLOC_LOG_SECONDARY_ZONE_WARN
#define ALLOC_LOG_SECONDARY_ZONE_WARN 1
#endif

/* ──────────── Трассировка операций ──────────── */

/**
 * Размер кольцевого буфера трассировки аллокаций (записей).
 * Хранит последние N операций alloc/free с размером, зоной и владельцем.
 * 0 — выключено.
 *
 * Чтение из отладчика: (gdb) p g_allocTrace
 */
#ifndef ALLOC_TRACE_BUFFER_SIZE
#define ALLOC_TRACE_BUFFER_SIZE 32U
#endif

/**
 * Логировать каждую аллокацию/деаллокацию через logF_ISR.
 * Формат: "alloc 64B Z0 TaskName\n" / "free 64B Z0 TaskName\n"
 * 0 — отключено, 1 — включено.
 * Требует ALLOC_ENABLE_LOGGING == 1.
 */
#ifndef ALLOC_TRACE_LOG_OPERATIONS
#define ALLOC_TRACE_LOG_OPERATIONS 0
#endif

/* ──────────── Обработчик ошибок кучи ──────────── */

#if ALLOC_TRACE_BUFFER_SIZE > 0
    #ifdef __cplusplus
    extern "C"
    #endif
    void alloc_trace_dump(void);

    /**
     * ALLOC_FAIL — при обнаружении порчи кучи/карантина.
     * Выводит кольцевой буфер последних операций, затем ассертит.
     */
    #define ALLOC_FAIL(msg) \
        do { alloc_trace_dump(); ALLOC_ASSERT(0 && (msg)); } while(0)
#else
    #define ALLOC_FAIL(msg) ALLOC_ASSERT(0 && (msg))
#endif
