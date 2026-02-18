/**
 * @file MpuGuard.hpp
 * @brief Абстракция защиты памяти через MPU.
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include "AllocConf.h"

namespace AllocCustom {

/**
 * @brief Управление MPU-регионами.
 *
 * На целевом MCU — реальная работа с MPU (реализация в отдельном .cpp).
 * На хосте — заглушка (MpuGuardStub.cpp).
 */
struct MpuGuard {
    /**
     * Защитить область памяти (read-only).
     * @return индекс региона MPU или -1 при неудаче.
     */
    static int  protect(uintptr_t addr, size_t size);
    static void unprotect(int regionIndex);
    static bool available();

    /* ── Утилиты ── */

    /** Наибольшая степень двойки ≤ value. */
    static size_t floorPow2(size_t value);

    /** Проверка: value — степень двойки. */
    static bool isPow2(size_t value);

    /** Выровнять адрес вниз к кратному alignment. */
    static uintptr_t alignDown(uintptr_t addr, size_t alignment);
};

} // namespace AllocCustom
