/*
 * Driving the Cannonball engine (port/glue.hpp on the Pico).
 *
 * The engine advances in 60 Hz "slots", as pico_shared's display pacing gave the Pico build: at the
 * default config.fps of 30 it runs on every second slot. main.cpp calls engine_slot() once for each
 * 1/60 s that has passed, and engine_render() once per picture it has time for.
 */

#pragma once

/* Bind the data image to the engine and boot it. False: no usable game data. */
bool engine_init(void);

/* One 60 Hz slot: controls, engine logic, sound. Returns true if the engine ticked, i.e. there is a new
 * picture to render. */
bool engine_slot(void);

/* Render the engine's current state into esp_screen's back picture (not presented). */
void engine_render(void);

/* Counters since the last call, then cleared. */
void engine_stats(unsigned *slots, unsigned *ticks);
