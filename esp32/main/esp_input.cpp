/*
 * Controls (port/input.cpp's merged pad on the Pico; here the console's own):
 *
 *   tilt          steers: the engine's analog wheel, centred where the console was held when the game
 *                 started, full lock at TILT_FULL g (as OpenMotoRacer: X - X_at_start, left end down = X < 0)
 *   BOOT          accelerate (held)       PWR    brake (tap)        [BOOT + PWR for 2 s: back to the menu]
 *                 (not the other way round: the AXP2101 powers the console off when PWR is held for 10 s)
 *   touch         in a race: left half = LOW gear, right half = HIGH gear
 *                 music selection: tilt off; BOOT / PWR or the left / right third of the screen = previous / next,
 *                 the middle = select.  Everywhere else (title, attract...): any touch = Start
 *
 * Only call esp_input_poll() from one task; it reads the IMU and touch over I2C.
 */

#include "esp_input.hpp"

#include <math.h>
#include <string.h>

#include "esp_board.hpp"
#include "esp_log.h"
#include "esp_screen.hpp"
#include "engine/outrun.hpp"
#include "sdl2/input.hpp"

static const char *TAG = "input";

#define TILT_FULL 0.45f  /* g from the start position for full lock (OMR) */
#define TILT_DEAD 0.03f  /* g */
#define TILT_SMOOTH 0.6f /* share of the new reading per poll (OMR: ~35 % per 5 ms; polled at 60 Hz here) */
#define TILT_MENU 0.5f   /* of full lock: LEFT / RIGHT for the engine's menus */

volatile int g_inj_tap, g_inj_tap_x, g_inj_gas, g_inj_brake, g_inj_steer;

static float s_x0;
static float s_x;
static bool s_have;
static int s_cal_left;
static float s_cal_sum;

void esp_input_init(void)
{
    input.init();
    s_have = false;
    s_cal_left = 12; /* ~0.2 s at 60 polls a second */
    s_cal_sum = 0;
}

static float steer_value(void)
{
    float g[3];
    if (!board_accel(g))
    {
        return 0;
    }

    if (s_cal_left > 0)
    {
        s_cal_sum += g[0];
        if (--s_cal_left == 0)
        {
            s_x0 = s_cal_sum / 12.0f;
            s_x = s_x0;
            s_have = true;
            ESP_LOGI(TAG, "tilt centre X = %.3f g", s_x0);
        }
        return 0;
    }

    s_x += (g[0] - s_x) * TILT_SMOOTH;
    float d = s_x - s_x0;
    const float a = fabsf(d);
    if (a < TILT_DEAD)
    {
        return 0;
    }
    float v = ((a - TILT_DEAD) / (TILT_FULL - TILT_DEAD)) * (d < 0 ? -1.0f : 1.0f);
    if (v > 1.0f)
    {
        v = 1.0f;
    }
    if (v < -1.0f)
    {
        v = -1.0f;
    }
    return v;
}

void esp_input_poll(void)
{
    const int state = outrun.game_state;
    const bool music = state == GS_MUSIC;
    const bool driving = state >= GS_START1 && state <= GS_INGAME;

    /* touch: a finger that was already down when the screen changed is ignored until it lifts, so the
     * tap that started a screen cannot also act on the next one */
    int tx = 0, ty = 0;
    bool touched = board_touch(&tx, &ty);
    if (g_inj_tap > 0)
    {
        g_inj_tap = g_inj_tap - 1;
        touched = true;
        tx = g_inj_tap_x;
        ty = SCR_H / 2;
    }
    static int s_state = -1;
    static bool s_wait_release;
    if (state != s_state)
    {
        s_state = state;
        s_wait_release = touched;
    }
    if (!touched)
    {
        s_wait_release = false;
    }
    if (s_wait_release)
    {
        touched = false;
    }
    const bool left = touched && tx < SCR_W / 3;
    const bool right = touched && tx >= 2 * SCR_W / 3;
    const bool left_half = touched && tx < SCR_W / 2;
    const bool right_half = touched && tx >= SCR_W / 2;

    float v = steer_value(); /* -1 (left) .. +1 (right) */
    if (g_inj_steer)
    {
        v = g_inj_steer / 100.0f;
    }

    input.gamepad = true;
    input.analog = 1; /* analog wheel and pedals */

    bool gas = board_boot_down() || g_inj_gas;
    bool brake = board_pwr_down() || g_inj_brake;

    if (music)
    {
        /* music selection: no tilt (it would read as left/right and flip the station). BOOT = previous,
         * PWR = next, or touch the left / right third; touch the middle to select. */
        input.a_wheel = Input::CENTRE;
        input.set_button(Input::LEFT, board_boot_down() || left);
        input.set_button(Input::RIGHT, board_pwr_down() || right);
        input.set_button(Input::START, touched && !left && !right);
        input.set_button(Input::GEAR1, false);
        input.set_button(Input::GEAR2, false);
        gas = brake = false;
    }
    else
    {
        input.a_wheel = Input::CENTRE + (int)(v * 127.0f);
        input.set_button(Input::LEFT, v < -TILT_MENU);
        input.set_button(Input::RIGHT, v > TILT_MENU);
        /* in a race: touch the left half for LOW gear, the right half for HIGH. Anywhere else (title,
         * attract, game over...) any touch is Start. */
        input.set_button(Input::GEAR1, driving && left_half);
        input.set_button(Input::GEAR2, driving && right_half);
        input.set_button(Input::START, touched && !driving);
    }

    input.a_accel = gas ? 0xFF : 0;
    input.a_brake = brake ? 0xFF : 0;
    input.set_button(Input::ACCEL, gas);
    input.set_button(Input::BRAKE, brake);
    input.set_button(Input::UP, false);
    input.set_button(Input::DOWN, false);
    input.set_button(Input::COIN, false);
}
