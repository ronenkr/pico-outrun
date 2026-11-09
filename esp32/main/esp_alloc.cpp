/*
 * The engine's two pools (port/outrun_alloc.hpp; port/alloc.cpp on the Pico), on the ESP32-S3:
 * PSRAM for big, cold buffers, internal RAM for what is touched per pixel or per scanline.
 */

#include "outrun_alloc.hpp"

#include "esp_heap_caps.h"

extern "C" void *outrun_psram_alloc(size_t bytes)
{
    /* zeroed: the engine's video buffers and the Pico's Frens::f_malloc-backed ones start cleared */
    return heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

extern "C" void outrun_psram_free(void *p)
{
    heap_caps_free(p);
}

extern "C" void *outrun_sram_alloc(size_t bytes)
{
    return heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

extern "C" void outrun_sram_free(void *p)
{
    heap_caps_free(p);
}
