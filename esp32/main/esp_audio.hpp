/*
 * The audio bridge between the Cannonball sound engine and the console's codec (port/audio.cpp on the Pico).
 *
 * The engine mixes the YM2151 and SegaPCM at config.sound.rate (22 050 Hz) into stereo buffers, one burst
 * per engine tick. The codec takes 22 050 Hz mono through board_audio_start(), which calls fill() from a
 * task on core 1 for 256 frames at a time. A ring buffer sits between the two.
 */

#pragma once

#include <stdint.h>

/* Start the codec (board_audio_start). False if the board has no audio. */
bool esp_audio_start(void);
void esp_audio_reset(void);

/* Start the sound task (core 1): from here on it alone runs the engine's sound program (osoundint.tick())
 * and the YM2151 / SegaPCM, topping the ring up as the codec drains it, so the music keeps its tempo and
 * pitch however busy the game loop is. The game side only queues sound commands (queue_sound), as on the
 * Pico's core 1. Call after the engine is initialised. */
void esp_audio_start_task(void);

/* Counters since the last call, then cleared: frames played, frames dropped because the ring was full,
 * frames of silence because it was empty. */
void esp_audio_stats(uint32_t *played, uint32_t *dropped, uint32_t *starved);

/* Peak and RMS (0..32767) of what went to the codec since the last call, then cleared. */
void esp_audio_level(int *peak, int *rms);
