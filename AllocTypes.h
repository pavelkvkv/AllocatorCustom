/**
 * @file AllocTypes.h
 * @brief Общие типы данных страничного аллокатора.
 *
 * Используется как из C, так и из C++ кода.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "AllocConf.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Метаданные аллоцированной области.
 *
 * Хранятся в отдельной таблице, индексированной по номеру стартовой страницы.
 * НЕ размещаются внутри области выделения — там только канарейки.
 */
typedef struct {
    uint32_t requestedSize;   /**< Запрошенный пользователем размер (байт) */
    uint16_t pageCount;       /**< Число выделенных страниц */
    uint16_t startPage;       /**< Индекс стартовой страницы (самопроверка) */
    uint32_t sequenceNum;     /**< Порядковый номер аллокации */
} AllocPageMeta;

/**
 * @brief Запись в таблице карантина.
 */
typedef struct {
    uint16_t startPage;       /**< Первая страница карантинной области */
    uint16_t pageCount;       /**< Число страниц */
    uint32_t requestedSize;   /**< Размер пользовательских данных */
    uint32_t freeSequence;    /**< Порядковый номер при освобождении (FIFO) */
    int8_t   mpuRegion;       /**< Регион MPU (-1 = не защищено) */
    uint8_t  zoneIndex;       /**< Индекс зоны */
    uint8_t  active;          /**< 1 = запись используется */
    uint8_t  reserved;        /**< Выравнивание */
} AllocQuarantineEntry;

/* ──────────── Трассировка операций ──────────── */

#if ALLOC_TRACE_BUFFER_SIZE > 0

/** Длина хранимого имени таска (с '\0'). */
#define ALLOC_TRACE_NAME_LEN 12U

/**
 * @brief Запись в кольцевом буфере трассировки.
 *
 * 24 байта: адрес, размер, имя таска (копия), зона, операция.
 * Имя таска копируется — безопасно после удаления временных задач.
 */
typedef struct {
    void*       addr;                       /**< Адрес пользовательских данных */
    uint32_t    size;                       /**< Запрошенный размер (байт) */
    char        task[ALLOC_TRACE_NAME_LEN]; /**< Имя таска (копия, усечённое) */
    uint8_t     zone;                       /**< Индекс зоны (0 = fast, 1 = slow) */
    uint8_t     op;                         /**< 'A' = alloc, 'F' = free */
    uint16_t    _reserved;                  /**< Выравнивание */
} AllocTraceEntry;

/**
 * @brief Кольцевой буфер трассировки.
 *
 * Глобальный экземпляр: g_allocTrace
 * Чтение из отладчика: (gdb) p g_allocTrace
 */
typedef struct {
    AllocTraceEntry entries[ALLOC_TRACE_BUFFER_SIZE];
    uint32_t head;          /**< Индекс следующей записи (по модулю) */
    uint32_t totalOps;      /**< Общий счётчик операций */
} AllocTraceRing;

extern AllocTraceRing g_allocTrace;

#endif /* ALLOC_TRACE_BUFFER_SIZE > 0 */

#ifdef __cplusplus
} /* extern "C" */
#endif
