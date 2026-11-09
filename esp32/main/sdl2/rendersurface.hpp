/*
 * The ESP32-S3's replacement for port/sdl2/rendersurface.hpp (which shadows upstream's; this one shadows
 * that, because esp32/main comes first on the include path).
 *
 * Same job: Render::draw_frame() turns the engine's 320x224 buffer of System 16 palette indices into
 * pixels. Here they are RGB565, squeezed to the LCD's 284 columns, and written into the picture that
 * esp_screen hands out. See esp_render.cpp.
 *
 * Render holds the 16 KB palette table that is read once per output pixel, so it is allocated in internal
 * RAM (operator new below) rather than in PSRAM, where every other allocation over 512 bytes goes.
 */

#pragma once

#include <stddef.h>

#include "sdl2/renderbase.hpp"
#include "outrun_alloc.hpp"

class Render : public RenderBase
{
public:
    Render() = default;
    ~Render() override = default;

    static void *operator new(size_t n) { return outrun_sram_alloc(n); }
    static void operator delete(void *p) { outrun_sram_free(p); }

    bool init(int src_width, int src_height, int scale, int video_mode, int scanlines) override;
    void disable() override;
    bool start_frame() override;
    bool finalize_frame() override;
    void draw_frame(uint16_t *pixels) override;
};
