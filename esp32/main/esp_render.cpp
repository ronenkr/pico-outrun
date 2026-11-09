/*
 * The ESP32-S3 renderer (port/render.cpp on the Pico): one pass from the engine's palette-index buffer
 * into the LCD picture, RGB565, squeezed from OutRun's 320 columns to the LCD's 284 by dropping 36
 * evenly spaced columns, 224 rows centred in 240.
 *
 * The index buffer has to exist (hwsprites reads pixels back to do shadows; see port/sdl2/rendersurface.hpp).
 */

#include "sdl2/rendersurface.hpp"

#include <math.h>
#include <string.h>

#include "esp_screen.hpp"
#include "frontend/config.hpp"

/* RenderBase::convert_palette(), as port/render.cpp has it but packing RGB565. r1/g1/b1 are 5-bit
 * System 16 components, widened to 8 bits as upstream does. The shadow copy at adr +
 * S16_PALETTE_ENTRIES is not optional: hwsprites writes that index for shadowed pixels. */
#define PACK565(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

void RenderBase::convert_palette(uint32_t adr, uint32_t r1, uint32_t g1, uint32_t b1)
{
    adr >>= 1;

    uint32_t r = r1 * 8;
    uint32_t g = g1 * 8;
    uint32_t b = b1 * 8;
    rgb[adr] = PACK565(r, g, b);

    // Shadow colours live in the upper half of the table
    r = r1 * shadow_multi / 31;
    g = g1 * shadow_multi / 31;
    b = b1 * shadow_multi / 31;
    rgb[adr + S16_PALETTE_ENTRIES] = PACK565(r, g, b);
}

void RenderBase::set_shadow_intensity(float f)
{
    shadow_multi = (int)roundf(255.0f * f);
}

/* source column of each LCD column: floor(x * 320 / 284) */
static uint16_t s_xmap[SCR_W];

bool Render::init(int src_width, int src_height, int /*scale*/, int /*video_mode*/, int /*scanlines*/)
{
    this->src_width = src_width;
    this->src_height = src_height;

    for (int x = 0; x < SCR_W; x++)
    {
        s_xmap[x] = (uint16_t)(x * src_width / SCR_W);
    }
    return true;
}

void Render::disable()
{
}

bool Render::start_frame()
{
    return true;
}

bool Render::finalize_frame()
{
    return true;
}

/* The hot loop: SCR_W x 224 palette lookups per picture. The letterbox rows above and below are black
 * from screen_blank_all() and never touched. */
void Render::draw_frame(uint16_t *pixels)
{
    const uint16_t *lut = rgb;
    const uint16_t *xmap = s_xmap;
    uint16_t *fb = screen_back();
    const int w = src_width;

    for (int y = 0; y < src_height; y++)
    {
        uint16_t *dst = fb + (y + OUTRUN_YOFFSET) * SCR_W;
        const uint16_t *src = pixels + y * w;
        for (int x = 0; x < SCR_W; x++)
        {
            dst[x] = lut[src[xmap[x]]];
        }
    }
}
