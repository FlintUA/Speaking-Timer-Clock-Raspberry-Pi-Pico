#include "mp3_probe.h"
#include "sd_fat32.h"
#include "pcm_ring.h"
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
static bool playing;
static void mp3_play_service(void);

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
    if (started || playing) {
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
    if (playing) { mp3_play_service(); return; }
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

// Full-file playback with a bounded per-loop decode workload.
// Never read SD or invoke Helix from the Bluetooth media callback.
#define MP3_PLAY_BLOCK 256u
#define MP3_PLAY_PREFILL 4096u
static int16_t play_pcm[MP3_PLAY_BLOCK * 2u];
static bool play_eof, play_first, play_decoder_open, play_paused;
static uint32_t play_src_rate, play_dst_rate, play_phase;
static uint32_t play_frames_decoded, play_frames_pushed;
static uint64_t play_decode_time_us;
static int16_t play_prev_l, play_prev_r;
static bool auto_pause_test, auto_pause_done;
static uint64_t auto_resume_at_us;
static uint32_t last_consumed, last_underruns;
static bool last_valid;
static void capture_play_stats(void) {
    last_consumed = clock_pcm_ring_consumed();
    last_underruns = clock_pcm_ring_underruns();
    last_valid = true;
}

bool mp3_play_active(void) { return playing; }
void mp3_play_status(void) {
    // Each successful PCM push must be reflected in the ring's produced
    // counter. Observe the raw counters rather than masking over-consumption.
    uint32_t ring_produced = clock_pcm_ring_produced();
    uint32_t ring_consumed = clock_pcm_ring_consumed();
    uint32_t buffered = clock_pcm_ring_available();
    if (playing && (ring_produced != play_frames_pushed ||
                    ring_consumed > ring_produced ||
                    ring_produced - ring_consumed != buffered)) {
        printf("[PCM-DIAG] INVARIANT_MISMATCH pushed=%lu ring_written=%lu read=%lu buffered=%lu\n",
               (unsigned long)play_frames_pushed,
               (unsigned long)ring_produced,
               (unsigned long)ring_consumed, (unsigned long)buffered);
    }
    printf("[MP3-PLAY] active=%s paused=%s buffered=%lu/%u source=%luHz target=%luHz decoded=%lu output=%lu consumed=%lu underruns=%lu eof=%s decode_us=%llu\n",
           playing ? "YES" : "NO", play_paused ? "YES" : "NO",
           (unsigned long)clock_pcm_ring_available(), CLOCK_PCM_RING_FRAMES,
           (unsigned long)play_src_rate, (unsigned long)play_dst_rate,
           (unsigned long)play_frames_decoded,
           (unsigned long)play_frames_pushed,
           (unsigned long)(playing ? clock_pcm_ring_consumed() :
                           (last_valid ? last_consumed : 0u)),
           (unsigned long)(playing ? clock_pcm_ring_underruns() :
                           (last_valid ? last_underruns : 0u)),
           play_eof ? "YES" : "NO",
           (unsigned long long)play_decode_time_us);
}
void mp3_play_stop(void) {
    if (playing) {
        mp3_play_status();
        puts("[MP3-PLAY] Stopped; A2DP returning to silence");
        if (play_decoder_open) {
            helix_mp3_deinit(&decoder);
            sd_fat32_close_read();
            play_decoder_open = false;
        }
    }
    if (playing) capture_play_stats();
    playing = false;
    play_paused = false;
    play_eof = false;
    auto_pause_test = false;
    clock_pcm_ring_reset();
}
bool mp3_play_pause(void) {
    if (!playing || play_paused) return false;
    play_paused = true;
    clock_pcm_ring_set_paused(true);
    puts("[MP3-PLAY] PAUSED (PCM preserved, SBC silence)");
    return true;
}
bool mp3_play_resume(void) {
    if (!playing || !play_paused) return false;
    play_paused = false;
    clock_pcm_ring_set_paused(false);
    puts("[MP3-PLAY] RESUMED");
    return true;
}
bool mp3_play_start(const char *path, uint32_t a2dp_rate) {
    if (started || playing || clock_pcm_ring_active()) {
        puts("[MP3-PLAY] Decoder or playback already active");
        return false;
    }
    if (a2dp_rate != 44100u) {
        printf("[MP3-PLAY] Unsupported A2DP rate %lu, expected 44100\n",
               (unsigned long)a2dp_rate);
        return false;
    }
    if (!sd_fat32_open_read(path)) {
        puts("[MP3-PLAY] Cannot open MP3 on microSD (run sd-test)");
        return false;
    }
    io.seek = decoder_seek;
    io.read = decoder_read;
    io.user_data = NULL;
    uint64_t init_at = time_us_64();
    int rc = helix_mp3_init(&decoder, &io);
    uint64_t init_elapsed = time_us_64() - init_at;
    if (rc) {
        printf("[MP3-PLAY] Helix init failed rc=%d\n", rc);
        sd_fat32_close_read();
        return false;
    }
    play_src_rate = helix_mp3_get_sample_rate(&decoder);
    play_dst_rate = a2dp_rate;
    if (play_src_rate != 48000u && play_src_rate != 44100u) {
        printf("[MP3-PLAY] Unsupported source rate %lu; only 44100 or 48000\n",
               (unsigned long)play_src_rate);
        helix_mp3_deinit(&decoder);
        sd_fat32_close_read();
        return false;
    }
    clock_pcm_ring_reset();
    playing = true;
    play_decoder_open = true;
    play_eof = false;
    play_paused = false;
    auto_pause_test = false;
    auto_pause_done = false;
    last_valid = false;
    play_first = true;
    play_phase = 0;
    play_frames_decoded = play_frames_pushed = 0;
    play_decode_time_us = init_elapsed;
    printf("[MP3-PLAY] START /%s: %luHz %lu bps -> 44100Hz, full file, gain 53.3%%\n",
           path, (unsigned long)play_src_rate,
           (unsigned long)helix_mp3_get_bitrate(&decoder));
    return true;
}
bool mp3_play_pause_test_start(const char *path, uint32_t a2dp_rate) {
    if (!mp3_play_start(path, a2dp_rate)) return false;
    auto_pause_test = true;
    auto_pause_done = false;
    puts("[MP3-TEST] Auto pause at 300 ms of output; resume after 1000 ms");
    return true;
}
static void mp3_play_service(void) {
    if (!playing) return;
    if (auto_pause_test && play_paused && auto_pause_done &&
        time_us_64() >= auto_resume_at_us) {
        if (mp3_play_resume()) {
            auto_pause_test = false;
            puts("[MP3-TEST] Auto resume completed");
        }
    }
    if (play_paused) return;
    if (auto_pause_test && !auto_pause_done &&
        clock_pcm_ring_active() &&
        clock_pcm_ring_consumed() >= (44100u * 300u / 1000u)) {
        if (mp3_play_pause()) {
            auto_pause_done = true;
            auto_resume_at_us = time_us_64() + 1000000u;
            puts("[MP3-TEST] Auto pause triggered; will resume after 1000 ms");
        }
        return;
    }
    if (!play_eof && clock_pcm_ring_free() >= MP3_PLAY_BLOCK + 4u &&
        clock_pcm_ring_available() <= CLOCK_PCM_RING_FRAMES - 512u) {
        uint64_t now = time_us_64();
        size_t received = helix_mp3_read_pcm_frames_s16(&decoder, play_pcm,
                                                       MP3_PLAY_BLOCK);
        play_decode_time_us += time_us_64() - now;
        if (!received) {
            play_eof = true;
            clock_pcm_ring_set_eof(true);
            if (play_decoder_open) {
                helix_mp3_deinit(&decoder);
                sd_fat32_close_read();
                play_decoder_open = false;
            }
        } else {
            play_frames_decoded += (uint32_t)received;
            for (size_t i = 0; i < received; ++i) {
                int16_t left = play_pcm[i * 2u];
                int16_t right = play_pcm[i * 2u + 1u];
                if (play_first) {
                    play_prev_l = left;
                    play_prev_r = right;
                    play_first = false;
                    continue;
                }
                // Linear interpolation over each pair of source samples.
                // Integer phase prevents drift across decoder blocks.
                while (play_phase < play_dst_rate) {
                    int32_t l = (int32_t)play_prev_l +
                        ((int64_t)((int32_t)left - (int32_t)play_prev_l) *
                         (int64_t)play_phase) / (int64_t)play_dst_rate;
                    int32_t r = (int32_t)play_prev_r +
                        ((int64_t)((int32_t)right - (int32_t)play_prev_r) *
                         (int64_t)play_phase) / (int64_t)play_dst_rate;
                    if (!clock_pcm_ring_push((int16_t)l, (int16_t)r)) {
                        puts("[MP3-PLAY] Ring overflow; stopping playback");
                        mp3_play_stop();
                        return;
                    }
                    ++play_frames_pushed;
                    play_phase += play_src_rate;
                }
                play_phase -= play_dst_rate;
                play_prev_l = left;
                play_prev_r = right;
            }
            // Final short Helix read is valid PCM. Next read signals EOF.
        }
    }
    if (!clock_pcm_ring_active() &&
        (clock_pcm_ring_available() >= MP3_PLAY_PREFILL ||
         (play_eof && clock_pcm_ring_available()))) {
        clock_pcm_ring_set_active(true);
        printf("[MP3-PLAY] Playback started, prebuffer=%lu frames\n",
               (unsigned long)clock_pcm_ring_available());
    }
    if (play_eof && clock_pcm_ring_available() == 0u) {
        mp3_play_status();
        puts("[MP3-PLAY] COMPLETE; resuming A2DP silence");
        capture_play_stats();
        playing = false;
        auto_pause_test = false;
        clock_pcm_ring_reset();
    }
}

#else
bool mp3_probe_active(void) { return false; }
bool mp3_probe_start(const char *path) {
    (void)path;
    puts("[MP3] Not compiled: enable CLOCK_ENABLE_MP3 and link external Helix");
    return false;
}
void mp3_probe_service(void) {}
bool mp3_play_start(const char *path, uint32_t rate) {
    (void)path; (void)rate;
    puts("[MP3-PLAY] MP3 support not compiled");
    return false;
}
void mp3_play_stop(void) {}
bool mp3_play_active(void) { return false; }
bool mp3_play_pause(void) { return false; }
bool mp3_play_pause_test_start(const char *path, uint32_t rate) {
    (void)path; (void)rate;
    puts("[MP3-TEST] MP3 support not compiled");
    return false;
}
bool mp3_play_resume(void) { return false; }
void mp3_play_status(void) {
    puts("[MP3-PLAY] MP3 support not compiled");
}
#endif
