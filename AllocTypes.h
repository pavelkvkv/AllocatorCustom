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

#ifdef __cplusplus
} /* extern "C" */
#endif
