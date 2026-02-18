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
 * @brief Хедер аллоцированной области (32 байта).
 *
 * Располагается в начале первой страницы выделенной области.
 * Контрольная сумма покрывает все поля кроме самой себя.
 */
typedef struct {
    uint32_t magic;           /**< ALLOC_PATTERN_HEADER_MAGIC */
    uint32_t requestedSize;   /**< Запрошенный пользователем размер (байт) */
    uint16_t startPage;       /**< Индекс первой страницы в зоне */
    uint16_t pageCount;       /**< Число выделенных страниц */
    uint8_t  zoneIndex;       /**< Индекс зоны */
    uint8_t  reserved[3];     /**< Резерв (выравнивание) */
    uint32_t sequenceNum;     /**< Порядковый номер аллокации */
    uint32_t reserved2;       /**< Задел: TaskHandle_t */
    uint32_t reserved3;       /**< Задел: доп. данные */
    uint32_t checksum;        /**< XOR слов [0..6] */
} AllocBlockHeader;

/**
 * @brief Футер аллоцированной области (32 байта).
 *
 * Дублирует критичные поля хедера для перекрёстной валидации.
 */
typedef struct {
    uint32_t magic;           /**< ALLOC_PATTERN_FOOTER_MAGIC */
    uint32_t requestedSize;   /**< Копия requestedSize */
    uint16_t startPage;       /**< Копия startPage */
    uint16_t pageCount;       /**< Копия pageCount */
    uint8_t  zoneIndex;       /**< Копия zoneIndex */
    uint8_t  reserved[3];     /**< Резерв */
    uint32_t sequenceNum;     /**< Копия sequenceNum */
    uint32_t reserved2;       /**< Задел: TaskHandle_t */
    uint32_t reserved3;       /**< Задел */
    uint32_t checksum;        /**< XOR слов [0..6] */
} AllocBlockFooter;

ALLOC_STATIC_ASSERT(sizeof(AllocBlockHeader) == ALLOC_HEADER_SIZE,
                     "AllocBlockHeader size must equal ALLOC_HEADER_SIZE");
ALLOC_STATIC_ASSERT(sizeof(AllocBlockFooter) == ALLOC_FOOTER_SIZE,
                     "AllocBlockFooter size must equal ALLOC_FOOTER_SIZE");

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

#ifdef __cplusplus
} /* extern "C" */
#endif
