/**
 * @file MpuGuardStub.cpp
 * @brief Заглушка MpuGuard (MPU недоступен или HOST_BUILD).
 */
#include "MpuGuard.hpp"

namespace AllocCustom {

int  MpuGuard::protect(uintptr_t /*addr*/, size_t /*size*/) { return -1; }
void MpuGuard::unprotect(int /*regionIndex*/) {}
bool MpuGuard::available() { return false; }

size_t MpuGuard::floorPow2(size_t value) {
    if (value == 0U) return 0U;
    size_t result = 1U;
    while (result * 2U <= value) {
        result *= 2U;
    }
    return result;
}

bool MpuGuard::isPow2(size_t value) {
    return value > 0U && (value & (value - 1U)) == 0U;
}

uintptr_t MpuGuard::alignDown(uintptr_t addr, size_t alignment) {
    ALLOC_ASSERT(isPow2(alignment));
    return addr & ~(static_cast<uintptr_t>(alignment) - 1U);
}

} // namespace AllocCustom
