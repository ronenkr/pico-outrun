#pragma once

/* Reset the latch and start the tilt calibration (the first ~0.2 s of polls). */
void esp_input_init(void);

/* Read tilt, buttons and touch into `input` (sdl2/input.hpp). Call once per 60 Hz slot, from one task. */
void esp_input_poll(void);

/* Test hooks for the serial console (esp_console.cpp): act as if the control were used. */
extern volatile int g_inj_tap;   /* polls of a touch still to inject ... */
extern volatile int g_inj_tap_x; /* ... at this x (0..283) */
extern volatile int g_inj_gas;   /* held while non-zero */
extern volatile int g_inj_brake;
extern volatile int g_inj_steer; /* -100 (full left) .. 100 (full right); 0 = off */
