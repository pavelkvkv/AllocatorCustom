# AllocatorCustomCpp — Страничный мультизонный аллокатор

## Обзор

Страничный аллокатор памяти для FreeRTOS с защитой от порчи и карантином.

### Ключевые возможности

- **Страничное выделение**: аллокации кратны размеру страницы (512 Б по умолчанию)
- **Канарейки**: каждая область обрамляется хедером и футером произвольного размера (повторяющийся магический паттерн)
- **Метаданные отдельно**: requestedSize, pageCount, sequenceNum хранятся в мета-таблице (AllocPageMeta), не в области данных
- **Карантин**: освобождённая память помечается паттерном и проверяется при следующих операциях
- **MPU-защита**: опциональная защита карантинных страниц через Cortex-M MPU
- **Мультизонность**: поддержка нескольких несмежных зон (внутренняя SRAM + QSPI SRAM)
- **Потокобезопасность**: все публичные методы защищены от конкурентного доступа

### Архитектура

```
pvPortMalloc/vPortFree       ← FreeRTOSHeapWrapper.c (thin passthrough)
    │
    ▼
AllocatorCustomCpp           ← Мультизонный координатор + лок
    │
    ├── PageAllocator[0]     ← Зона 0 (fast SRAM)
    │     ├── PageBitmap × 2 ← inUse / allocated
    │     ├── QuarantineTable
    │     └── BlockGuard     ← header/footer
    │
    └── PageAllocator[1]     ← Зона 1 (slow QSPI)
          └── ...
```

### Конфигурация

Все параметры в `AllocConf.h` переопределяются через `-D` флаги.

### Интеграция

```cmake
add_subdirectory(Components/AllocatorCustomCpp)
target_link_libraries(MyApp PRIVATE AllocatorCustomCpp)
```
