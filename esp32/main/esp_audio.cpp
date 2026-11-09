#include "esp_audio.hpp"

#include <algorithm>
#include <atomic>
#include <math.h>

#include "esp_board.hpp"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "engine/audio/osoundint.hpp"
#include "frontend/config.hpp"

static const char *TAG = "audio";

/* The small speaker cannot reproduce low frequencies: a 200 Hz high-pass and a gain before the codec make
 * the engine and bass audible (porter's guide, 2.6). HPF_A = 1 / (1 + 2 pi 200 / 22050) */
#define HPF_A 0.9461f
#define GAIN 1.5f

/* in mono frames, a power of two: more than a burst (735 at 30 Hz) plus what the codec has not drained */
#define RING_FRAMES 4096
#define RING_MASK (RING_FRAMES - 1)

static int16_t s_ring[RING_FRAMES];
static std::atomic<uint32_t> s_head; /* written by the game task, in frames */
static std::atomic<uint32_t> s_tail; /* written by the audio task */
static std::atomic<uint32_t> s_played, s_dropped, s_starved;
static volatile int s_peak;
static volatile uint64_t s_sumsq;
static volatile uint32_t s_nsamp;

void esp_audio_reset(void)
{
    s_tail.store(s_head.load());
}

/* Called by the audio task on core 1: 256 mono frames at a time */
static void fill(int16_t *out, int frames)
{
    static float hp_y, hp_x;

    uint32_t tail = s_tail.load(std::memory_order_relaxed);
    const uint32_t head = s_head.load(std::memory_order_acquire);
    uint32_t avail = head - tail;
    const uint32_t n = std::min<uint32_t>(avail, (uint32_t)frames);

    for (int i = 0; i < frames; i++)
    {
        float x = 0;
        if ((uint32_t)i < n)
        {
            x = (float)s_ring[(tail + i) & RING_MASK];
        }
        hp_y = HPF_A * (hp_y + x - hp_x);
        hp_x = x;
        float v = hp_y * GAIN;
        if (v > 32767.0f)
        {
            v = 32767.0f;
        }
        if (v < -32768.0f)
        {
            v = -32768.0f;
        }
        out[i] = (int16_t)v;
        const int a = out[i] < 0 ? -out[i] : out[i];
        if (a > s_peak)
        {
            s_peak = a;
        }
        s_sumsq = s_sumsq + (uint64_t)((int32_t)out[i] * (int32_t)out[i]);
        s_nsamp = s_nsamp + 1;
    }
    s_tail.store(tail + n, std::memory_order_release);
    s_played += n;
    if (n < (uint32_t)frames)
    {
        s_starved += (uint32_t)frames - n;
    }
}

bool esp_audio_start(void)
{
    if (config.sound.rate != 22050)
    {
        ESP_LOGW(TAG, "engine sound rate is %d, the codec runs at 22050", config.sound.rate);
    }
    return board_audio_start(config.sound.rate, fill);
}

/* One engine sound frame: the YM2151 and SegaPCM mixed into the ring. Only the sound task calls this. */
static void audio_frame(void)
{
    if (!config.sound.enabled)
    {
        return;
    }

    osoundint.pcm->stream_update();
    osoundint.ym->stream_update();

    const int16_t *pcm = osoundint.pcm->get_buffer();
    const int16_t *ym = osoundint.ym->get_buffer();

    /* buffer_size counts interleaved samples (frames * channels); both chips are stereo at the same rate
     * and fps, but take the smaller rather than trust that */
    const uint32_t samples = std::min(osoundint.pcm->buffer_size, osoundint.ym->buffer_size);

    uint32_t head = s_head.load(std::memory_order_relaxed);
    const uint32_t tail = s_tail.load(std::memory_order_acquire);

    for (uint32_t i = 0; i + 1 < samples; i += 2)
    {
        if (head - tail >= RING_FRAMES)
        {
            s_dropped += (samples - i) / 2; /* the codec is not keeping up: drop the tail of this burst */
            break;
        }
        int32_t l = (int32_t)pcm[i] + (int32_t)ym[i];
        int32_t r = (int32_t)pcm[i + 1] + (int32_t)ym[i + 1];
        l = std::min<int32_t>(std::max<int32_t>(l, INT16_MIN), INT16_MAX); /* same clip as upstream's mix */
        r = std::min<int32_t>(std::max<int32_t>(r, INT16_MIN), INT16_MAX);
        s_ring[head & RING_MASK] = (int16_t)((l + r) / 2);
        head++;
    }
    s_head.store(head, std::memory_order_release);
}

/* The sound task. The engine's sound program runs at the engine rate (config.fps), and each tick yields
 * rate/fps frames. Rather than a timer, it is paced by the codec: whenever the ring holds less than one
 * burst, run another tick. That makes the music exactly as fast as the codec's clock. */
static void sound_task(void *)
{
    const uint32_t burst = (uint32_t)config.sound.rate / (uint32_t)config.fps;
    for (;;)
    {
        const uint32_t fill = s_head.load(std::memory_order_relaxed) - s_tail.load(std::memory_order_acquire);
        if (fill <= burst)
        {
            osoundint.tick();
            audio_frame();
        }
        else
        {
            vTaskDelay(1);
        }
    }
}

void esp_audio_start_task(void)
{
    xTaskCreatePinnedToCore(sound_task, "sound", 8192, NULL, 10, NULL, 1);
}

void esp_audio_stats(uint32_t *played, uint32_t *dropped, uint32_t *starved)
{
    *played = s_played.exchange(0);
    *dropped = s_dropped.exchange(0);
    *starved = s_starved.exchange(0);
}

void esp_audio_level(int *peak, int *rms)
{
    const uint32_t n = s_nsamp;
    *peak = s_peak;
    *rms = n ? (int)sqrt((double)s_sumsq / n) : 0;
    s_peak = 0;
    s_sumsq = 0;
    s_nsamp = 0;
}
