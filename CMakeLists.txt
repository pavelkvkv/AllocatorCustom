cmake_minimum_required(VERSION 3.16)

# =============================================================================
# AllocatorCustomCpp — Страничный мультизонный аллокатор
# =============================================================================
#
# Страничная аллокация с хедерами/футерами, карантином
# освобождённой памяти и опциональной защитой через MPU.
#
# Зависимости:
#   - freertos_kernel (для FreeRTOS.h, task.h)
#
# ВАЖНО: Не включайте FREERTOS_HEAP в CMake при использовании этого модуля!
#

add_library(AllocatorCustomCpp STATIC
    PageBitmap.cpp
    BlockGuard.cpp
    Quarantine.cpp
    MpuGuardStub.cpp
    PageAllocator.cpp
    AllocatorCustomCpp.cpp
    FreeRTOSHeapWrapper.c
)

target_include_directories(AllocatorCustomCpp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(AllocatorCustomCpp
    PUBLIC
        freertos_kernel
)

add_library(Project::AllocatorCustomCpp ALIAS AllocatorCustomCpp)
