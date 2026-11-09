/*
 * Finding the game data on the ESP32-S3's SD card (port/outrun_sdload.cpp on the Pico, with stdio instead
 * of FatFs and heap_caps instead of Frens::f_malloc). The container (outrun_data.c) and the packer
 * (outrun_pack.c) are the Pico build's own.
 */

#include "esp_data.hpp"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"

#include "outrun_data_priv.h"
#include "outrun_pack.h"

static const char *TAG = "data";

/* Free PSRAM we want to keep after the image and the packer's scratch: the engine's own use is ~0.5 MB
 * (video, tiles, sprites), the three LCD pictures 0.4 MB, plus the audio and stacks. */
#define OUTRUN_PSRAM_MARGIN (900u * 1024u)

/* 31 ROM files, then tiles, road, sprites and the closing checksum */
#define OUTRUN_STEPS (OUTRUN_PACK_FILE_COUNT + 4)

#define READ_CHUNK (32u * 1024u)

/* ------------------------------------------------------------------------- */
/* Failure state, reported by the accessors in outrun_data.h                  */
/* ------------------------------------------------------------------------- */

static outrun_data_error_t s_err;
static int s_detail;
static const char *s_bad[OUTRUN_DATA_MAX_BAD_FILES];
static int s_bad_count;
static char s_romdir[96];
static bool s_from_roms;

static void fail(outrun_data_error_t err, int detail)
{
    s_err = err;
    s_detail = detail;
}

outrun_data_error_t outrun_data_error(void) { return s_err; }
int outrun_data_error_detail(void) { return s_detail; }
int outrun_data_bad_file_count(void) { return s_bad_count; }
const char *outrun_data_bad_file(int idx)
{
    return (idx < 0 || idx >= s_bad_count || idx >= OUTRUN_DATA_MAX_BAD_FILES) ? NULL : s_bad[idx];
}
const char *outrun_data_romdir(void) { return s_romdir; }
bool outrun_data_built_from_roms(void) { return s_from_roms; }

static uint32_t free_psram(void)
{
    return (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

/* ------------------------------------------------------------------------- */
/* Route 1: the packed image                                                  */
/* ------------------------------------------------------------------------- */

static bool load_image(const char *dir, outrun_data_progress_fn progress)
{
    char path[128];
    snprintf(path, sizeof path, "%s/" OUTRUN_IMAGE_NAME, dir);

    FILE *f = fopen(path, "rb");
    if (!f)
    {
        ESP_LOGI(TAG, "no %s", path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    const uint32_t want = outrun_pack_image_size();
    if (size != (long)want)
    {
        ESP_LOGW(TAG, "%s is %ld bytes, expected %lu: ignoring it", path, size, (unsigned long)want);
        fclose(f);
        return false;
    }
    if (free_psram() < want + OUTRUN_PSRAM_MARGIN)
    {
        ESP_LOGE(TAG, "PSRAM: %lu free, need %lu", (unsigned long)free_psram(),
                 (unsigned long)(want + OUTRUN_PSRAM_MARGIN));
        fail(OUTRUN_DATA_ERR_NO_MEMORY, (int)((want + OUTRUN_PSRAM_MARGIN - free_psram()) / 1024u));
        fclose(f);
        return false;
    }

    /* the engine reads TILES and SPRITES as uint32 out of this buffer */
    uint8_t *image = (uint8_t *)heap_caps_aligned_alloc(16, want, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!image)
    {
        fail(OUTRUN_DATA_ERR_NO_MEMORY, 0);
        fclose(f);
        return false;
    }

    const int64_t t0 = esp_timer_get_time();
    uint32_t got = 0;
    while (got < want)
    {
        const uint32_t n = want - got < READ_CHUNK ? want - got : READ_CHUNK;
        if (fread(image + got, 1, n, f) != n)
        {
            ESP_LOGE(TAG, "read error at %lu of %lu", (unsigned long)got, (unsigned long)want);
            fclose(f);
            heap_caps_free(image);
            fail(OUTRUN_DATA_ERR_READ, errno);
            return false;
        }
        got += n;
        if (progress)
        {
            progress((int)(got / 1024u), (int)(want / 1024u), "reading " OUTRUN_IMAGE_NAME);
        }
    }
    fclose(f);
    ESP_LOGI(TAG, "%s read in %lld ms", path, (long long)((esp_timer_get_time() - t0) / 1000));

    /* the header's CRC covers everything after itself */
    const outrun_data_header_t *h = (const outrun_data_header_t *)image;
    const uint32_t crc = esp_rom_crc32_le(0, image + 16, want - 16);
    if (crc != h->crc32)
    {
        ESP_LOGE(TAG, "image CRC %08lx, header says %08lx", (unsigned long)crc, (unsigned long)h->crc32);
        heap_caps_free(image);
        fail(OUTRUN_DATA_ERR_BAD_CRC, 0);
        return false;
    }
    if (!outrun_data_adopt_psram(image, want))
    {
        heap_caps_free(image);
        fail(OUTRUN_DATA_ERR_BAD_IMAGE, 0);
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------------- */
/* Route 2: raw ROM files, packed here                                        */
/* ------------------------------------------------------------------------- */

namespace
{
    struct LoadCtx
    {
        const char *dir;
        int err;
        outrun_data_progress_fn progress;
        int files_done;
        int phases_done;
        int64_t mark_us;
        int64_t read_us;
    };
} // namespace

static void *io_open(void *ctx, const char *name)
{
    LoadCtx *c = (LoadCtx *)ctx;
    char path[160];
    snprintf(path, sizeof path, "%s/%s", c->dir, name);
    return fopen(path, "rb");
}

static long io_read(void *ctx, void *handle, void *buf, uint32_t len)
{
    LoadCtx *c = (LoadCtx *)ctx;
    const size_t n = fread(buf, 1, len, (FILE *)handle);
    if (n == 0 && ferror((FILE *)handle))
    {
        c->err = errno;
        return -1;
    }
    return (long)n;
}

static void io_close(void *ctx, void *handle)
{
    (void)ctx;
    fclose((FILE *)handle);
}

static void io_on_file(void *ctx, int done, int total, const char *name, uint32_t got,
                       uint32_t want_crc, uint32_t got_crc, outrun_file_state_t state)
{
    LoadCtx *c = (LoadCtx *)ctx;

    switch (state)
    {
    case OUTRUN_FILE_OK:
        ESP_LOGI(TAG, "%2d/%d %-16s %6lu bytes crc %08lx ok", done, total, name, (unsigned long)got,
                 (unsigned long)got_crc);
        break;
    case OUTRUN_FILE_MISSING:
        ESP_LOGW(TAG, "%2d/%d %-16s MISSING", done, total, name);
        break;
    case OUTRUN_FILE_SHORT:
        ESP_LOGW(TAG, "%2d/%d %-16s SHORT, read %lu bytes (errno %d)", done, total, name,
                 (unsigned long)got, c->err);
        break;
    case OUTRUN_FILE_BAD_CRC:
        ESP_LOGW(TAG, "%2d/%d %-16s BAD CRC expected %08lx found %08lx", done, total, name,
                 (unsigned long)want_crc, (unsigned long)got_crc);
        break;
    }

    if (state != OUTRUN_FILE_OK)
    {
        if (s_bad_count < OUTRUN_DATA_MAX_BAD_FILES)
        {
            s_bad[s_bad_count] = name;
        }
        s_bad_count++;
    }

    c->files_done = done;
    if (c->progress)
    {
        static char label[32];
        snprintf(label, sizeof label, "%s (%d/%d)", name, done, total);
        c->progress(c->files_done + c->phases_done, OUTRUN_STEPS, label);
    }
}

static void io_on_phase(void *ctx, const char *phase)
{
    LoadCtx *c = (LoadCtx *)ctx;
    const int64_t now = esp_timer_get_time();
    const int64_t elapsed = now - c->mark_us;
    c->mark_us = now;

    if (strcmp(phase, "read") == 0)
    {
        c->read_us += elapsed;
        return;
    }
    ESP_LOGI(TAG, "%s decoded in %lld ms", phase, (long long)(elapsed / 1000));

    c->phases_done++;
    if (c->progress)
    {
        static char label[32];
        snprintf(label, sizeof label, "decoding %s", phase);
        c->progress(c->files_done + c->phases_done, OUTRUN_STEPS, label);
    }
}

/* One file from each region family, so a partial set still identifies the directory */
static const int probe_idx[] = {0, 8, 16, 24, 25};

static int count_probe_hits(const char *dir)
{
    int hits = 0;
    for (unsigned i = 0; i < sizeof probe_idx / sizeof probe_idx[0]; i++)
    {
        char path[160];
        snprintf(path, sizeof path, "%s/%s", dir, outrun_pack_filename(probe_idx[i]));
        FILE *f = fopen(path, "rb");
        if (f)
        {
            fclose(f);
            hits++;
        }
    }
    return hits;
}

/* `dir` first, then `dir`/outrun (extracting a MAME zip commonly produces that); the likelier wins */
static bool choose_romdir(const char *dir)
{
    char nested[128];
    snprintf(nested, sizeof nested, "%s/outrun", dir);
    const int flat = count_probe_hits(dir);
    const int deep = count_probe_hits(nested);
    ESP_LOGI(TAG, "ROM probe: %s %d/%u, %s %d/%u", dir, flat,
             (unsigned)(sizeof probe_idx / sizeof probe_idx[0]), nested, deep,
             (unsigned)(sizeof probe_idx / sizeof probe_idx[0]));
    if (flat == 0 && deep == 0)
    {
        snprintf(s_romdir, sizeof s_romdir, "%s", dir);
        return false;
    }
    snprintf(s_romdir, sizeof s_romdir, "%s", deep > flat ? nested : dir);
    return true;
}

static bool build_from_roms(outrun_data_progress_fn progress)
{
    const uint32_t image_size = outrun_pack_image_size();
    const uint32_t need = image_size + OUTRUN_PACK_OVERHEAD + OUTRUN_PSRAM_MARGIN;
    if (free_psram() < need)
    {
        ESP_LOGE(TAG, "PSRAM: %lu free, need %lu", (unsigned long)free_psram(), (unsigned long)need);
        fail(OUTRUN_DATA_ERR_NO_MEMORY, (int)((need - free_psram()) / 1024u));
        return false;
    }

    const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    uint8_t *image = (uint8_t *)heap_caps_aligned_alloc(16, image_size, caps);
    uint8_t *scratch = (uint8_t *)heap_caps_malloc(OUTRUN_PACK_CHUNK, caps);
    uint8_t *src = (uint8_t *)heap_caps_malloc(OUTRUN_PACK_SRC_SIZE, caps);
    if (!image || !scratch || !src)
    {
        heap_caps_free(image);
        heap_caps_free(scratch);
        heap_caps_free(src);
        fail(OUTRUN_DATA_ERR_NO_MEMORY, 0);
        return false;
    }

    LoadCtx c;
    memset(&c, 0, sizeof c);
    c.dir = s_romdir;
    c.progress = progress;
    c.mark_us = esp_timer_get_time();
    const int64_t start_us = c.mark_us;

    outrun_pack_io_t io;
    memset(&io, 0, sizeof io);
    io.open = io_open;
    io.read = io_read;
    io.close = io_close;
    io.on_file = io_on_file;
    io.on_phase = io_on_phase;
    io.ctx = &c;

    outrun_pack_status_t st;
    const outrun_pack_result_t rc = outrun_pack_build(&io, image, image_size, scratch, OUTRUN_PACK_CHUNK,
                                                      src, OUTRUN_PACK_SRC_SIZE, &st);
    heap_caps_free(scratch);
    heap_caps_free(src);

    ESP_LOGI(TAG, "%d files read in %lld ms", OUTRUN_PACK_FILE_COUNT, (long long)(c.read_us / 1000));

    if (rc != OUTRUN_PACK_OK)
    {
        ESP_LOGE(TAG, "pack FAILED: %d missing, %d short, %d bad CRC of %d files", st.missing,
                 st.short_read, st.bad_crc, OUTRUN_PACK_FILE_COUNT);
        heap_caps_free(image);
        switch (rc)
        {
        case OUTRUN_PACK_ERR_MISSING: fail(OUTRUN_DATA_ERR_MISSING, st.missing); break;
        case OUTRUN_PACK_ERR_SHORT: fail(OUTRUN_DATA_ERR_READ, c.err); break;
        case OUTRUN_PACK_ERR_BAD_CRC: fail(OUTRUN_DATA_ERR_BAD_CRC, st.bad_crc); break;
        default: fail(OUTRUN_DATA_ERR_BAD_IMAGE, (int)rc); break;
        }
        return false;
    }

    ESP_LOGI(TAG, "image %lu bytes built in %lld ms", (unsigned long)st.image_size,
             (long long)((esp_timer_get_time() - start_us) / 1000));

    if (!outrun_data_adopt_psram(image, image_size))
    {
        heap_caps_free(image);
        fail(OUTRUN_DATA_ERR_BAD_IMAGE, 0);
        return false;
    }
    s_from_roms = true;
    return true;
}

/* ------------------------------------------------------------------------- */

bool outrun_data_init(const char *dir, bool sd_mounted, outrun_data_progress_fn progress)
{
    s_err = OUTRUN_DATA_ERR_NONE;
    s_detail = 0;
    s_bad_count = 0;
    s_romdir[0] = 0;
    s_from_roms = false;

    if (!sd_mounted)
    {
        fail(OUTRUN_DATA_ERR_NO_SD, 0);
        return false;
    }
    if (!dir || !dir[0])
    {
        fail(OUTRUN_DATA_ERR_NO_ROMS, 0);
        return false;
    }
    snprintf(s_romdir, sizeof s_romdir, "%s", dir);

    if (load_image(dir, progress))
    {
        ESP_LOGI(TAG, "using the packed image from %s", dir);
        return true;
    }
    if (s_err != OUTRUN_DATA_ERR_NONE && s_err != OUTRUN_DATA_ERR_BAD_CRC)
    {
        return false; /* the image exists but cannot be used (memory, read error): ROMs will not help */
    }
    s_err = OUTRUN_DATA_ERR_NONE; /* no image, or a corrupt one: try the ROMs */

    if (!choose_romdir(dir))
    {
        fail(OUTRUN_DATA_ERR_NO_ROMS, 0);
        return false;
    }
    const bool ok = build_from_roms(progress);
    if (ok)
    {
        ESP_LOGI(TAG, "using the image built from the ROMs in %s", s_romdir);
    }
    return ok;
}

/* ------------------------------------------------------------------------- */
/* Saving the built image, so the next boot is fast                           */
/* ------------------------------------------------------------------------- */

bool outrun_data_save_image(const char *dir, outrun_data_progress_fn progress)
{
    const uint8_t *image = outrun_data_base();
    if (!image)
    {
        return false;
    }
    const outrun_data_header_t *h = (const outrun_data_header_t *)image;
    const uint32_t size = h->total_size;

    char tmp[128], dst[128];
    snprintf(tmp, sizeof tmp, "%s/" OUTRUN_IMAGE_NAME ".tmp", dir);
    snprintf(dst, sizeof dst, "%s/" OUTRUN_IMAGE_NAME, dir);

    FILE *f = fopen(tmp, "wb");
    if (!f)
    {
        ESP_LOGW(TAG, "cannot write %s (errno %d)", tmp, errno);
        return false;
    }
    bool ok = true;
    for (uint32_t off = 0; ok && off < size; off += READ_CHUNK)
    {
        const uint32_t n = size - off < READ_CHUNK ? size - off : READ_CHUNK;
        ok = fwrite(image + off, 1, n, f) == n;
        if (progress)
        {
            progress((int)((off + n) / 1024u), (int)(size / 1024u), "saving " OUTRUN_IMAGE_NAME);
        }
    }
    ok = (fflush(f) == 0) && ok;
    fsync(fileno(f));
    ok = (fclose(f) == 0) && ok;
    if (ok)
    {
        remove(dst); /* FAT's rename does not replace */
        ok = rename(tmp, dst) == 0;
    }
    if (!ok)
    {
        ESP_LOGW(TAG, "saving %s failed (errno %d)", dst, errno);
        remove(tmp);
        return false;
    }
    ESP_LOGI(TAG, "saved %s (%lu bytes)", dst, (unsigned long)size);
    return true;
}
