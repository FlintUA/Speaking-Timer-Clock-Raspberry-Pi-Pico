#include "mp3_probe.h"
#include "sd_fat32.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

#ifdef CLOCK_ENABLE_MP3
#include "helix_mp3.h"
#define MP3_PROBE_MAX_BLOCKS 16u
#define MP3_PROBE_PCM_FRAMES 256u
static helix_mp3_t decoder;
static helix_mp3_io_t io;
static int16_t pcm[MP3_PROBE_PCM_FRAMES * 2u];
static bool started;
static unsigned blocks;
static uint32_t decoded_frames;
static uint64_t cpu_us;

static int decoder_seek(void *ctx, int offset) {
    (void)ctx;
    return offset >= 0 && sd_fat32_seek((uint32_t)offset) ? 0 : -1;
}
static size_t decoder_read(void *ctx, void *data, size_t bytes) {
    (void)ctx;
    return sd_fat32_read(data, bytes);
}
bool mp3_probe_active(void) { return started; }
bool mp3_probe_start(const char *path) {
    if (started) {
        puts("[MP3] Probe already active");
        return false;
    }
    if (!sd_fat32_open_read(path)) {
        puts("[MP3] Cannot open file; run sd-test first and use 8.3 path");
        return false;
    }
    io.seek = decoder_seek;
    io.read = decoder_read;
    io.user_data = NULL;
    uint64_t begin = time_us_64();
    int result = helix_mp3_init(&decoder, &io);
    uint64_t elapsed = time_us_64() - begin;
    if (result != 0) {
        printf("[MP3] Decoder initialization failed: %d\n", result);
        sd_fat32_close_read();
        return false;
    }
    started = true;
    blocks = 0;
    decoded_frames = 0;
    cpu_us = elapsed;
    printf("[MP3] Initial frame decoded: rate=%lu Hz bitrate=%lu bps, init=%llu us\n",
           (unsigned long)helix_mp3_get_sample_rate(&decoder),
           (unsigned long)helix_mp3_get_bitrate(&decoder),
           (unsigned long long)elapsed);
    return true;
}
void mp3_probe_service(void) {
    if (!started) return;
    uint64_t begin = time_us_64();
    size_t n = helix_mp3_read_pcm_frames_s16(&decoder, pcm, MP3_PROBE_PCM_FRAMES);
    uint64_t duration = time_us_64() - begin;
    cpu_us += duration;
    decoded_frames += (uint32_t)n;
    ++blocks;
    if (n && blocks < MP3_PROBE_MAX_BLOCKS) return;
    printf("[MP3] %s: pcm_frames=%lu blocks=%u sample_rate=%lu bitrate=%lu total_decode_us=%llu (no audio output)\n",
           n ? "PASS" : "EOF",
           (unsigned long)decoded_frames, blocks,
           (unsigned long)helix_mp3_get_sample_rate(&decoder),
           (unsigned long)helix_mp3_get_bitrate(&decoder),
           (unsigned long long)cpu_us);
    helix_mp3_deinit(&decoder);
    sd_fat32_close_read();
    started = false;
}
#else
bool mp3_probe_active(void) { return false; }
bool mp3_probe_start(const char *path) {
    (void)path;
    puts("[MP3] Not compiled: enable CLOCK_ENABLE_MP3 and link external Helix");
    return false;
}
void mp3_probe_service(void) {}
#endif
