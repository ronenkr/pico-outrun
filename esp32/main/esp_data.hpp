/*
 * ESP32 additions to the data image API of port/outrun_data.h.
 *
 * outrun_data_init(dir, sd_mounted, progress) (port/outrun_data.h; implemented in esp_data.cpp) finds the
 * game data in `dir`, normally /sdcard/games/outrun/data:
 *
 *   1. outrun-data.bin - the packed image tools/mkoutrundata writes on a PC (or that step 2 saved):
 *      read into PSRAM and checked against its CRC. Boots in about a second.
 *   2. otherwise the raw MAME 'outrun' revision B ROM files, in `dir` or `dir`/outrun: CRC-checked,
 *      decoded into the same image in PSRAM by port/outrun_pack.c (a few seconds).
 */

#pragma once

#include "outrun_data.h"

/* The path of the packed image inside `dir`. */
#define OUTRUN_IMAGE_NAME "outrun-data.bin"

/* After a build from ROMs: write the image to <dir>/outrun-data.bin (temp file, then rename) so the
 * next boot takes route 1. Returns false if the card refuses; the game runs regardless. */
bool outrun_data_save_image(const char *dir, outrun_data_progress_fn progress);

/* True if the running image was built from ROMs (and so is worth saving). */
bool outrun_data_built_from_roms(void);
