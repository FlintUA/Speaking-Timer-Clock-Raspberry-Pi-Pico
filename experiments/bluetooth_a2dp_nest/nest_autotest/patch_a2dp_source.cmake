# Generate a locally modified copy of the upstream BTstack A2DP example.
# The upstream file is not redistributed in this repository.
# Fail on SDK source changes instead of silently using an unmodified infinite sine.

if (NOT DEFINED UPSTREAM_SOURCE OR NOT DEFINED PATCHED_SOURCE)
    message(FATAL_ERROR "UPSTREAM_SOURCE and PATCHED_SOURCE are required")
endif()

file(READ "${UPSTREAM_SOURCE}" _source)

# Direct connect to an already known Bluetooth MAC without requiring inquiry mode.
# The upstream example only connects after a discoverable inquiry result.
# Keep the MAC outside firmware source; receive it over USB for this test.
set(_old_scan_function [=[
static void a2dp_source_demo_start_scanning(void){
]=])
set(_new_scan_function [=[
static const char *clock_autoconnect_target = "__CLOCK_AUTO_TARGET__";
static bool clock_a2dp_connect_address(const char *text){
    if (text == NULL || strlen(text) != 17u) return false;
    for (unsigned i = 0; i < 17u; ++i){
        if ((i % 3u) == 2u){
            if (text[i] != ':') return false;
        } else {
            if (!((text[i] >= '0' && text[i] <= '9') ||
                  (text[i] >= 'a' && text[i] <= 'f') ||
                  (text[i] >= 'A' && text[i] <= 'F'))) return false;
        }
    }
    if (media_tracker.a2dp_cid != 0){
        printf("[BT] Already connected or establishing; disconnect first\n");
        return false;
    }
    sscanf_bd_addr(text, device_addr);
    if (scan_active){
        scan_active = false;
        gap_inquiry_stop();
    }
    uint8_t status = a2dp_source_establish_stream(device_addr, &media_tracker.a2dp_cid);
    printf("[BT] Direct connect %s status=0x%02x\n", text, status);
    return status == ERROR_CODE_SUCCESS;
}

bool clock_a2dp_connect(const char *address){
    return clock_a2dp_connect_address(address);
}

// All reconnect callbacks run on the BTstack event loop.
static btstack_timer_source_t clock_reconnect_timer;
static bool clock_reconnect_pending;
static unsigned clock_reconnect_failures;
static const uint32_t clock_backoff_ms[] = {5000u, 10000u, 30000u};
static void clock_reconnect_schedule(void);

static void clock_reconnect_cancel(void){
    if (!clock_reconnect_pending) return;
    btstack_run_loop_remove_timer(&clock_reconnect_timer);
    clock_reconnect_pending = false;
}

static void clock_reconnect_timer_handler(btstack_timer_source_t *timer){
    (void)timer;
    clock_reconnect_pending = false;
    if (clock_autoconnect_target[0] == 0) return;
    if (media_tracker.a2dp_cid != 0) return;
    printf("[BT] Reconnect attempt %u to %s\n",
           clock_reconnect_failures, clock_autoconnect_target);
    if (!clock_a2dp_connect_address(clock_autoconnect_target)){
        clock_reconnect_schedule();
    }
}

static void clock_reconnect_schedule(void){
    if (clock_autoconnect_target[0] == 0 || clock_reconnect_pending) return;
    uint32_t index = clock_reconnect_failures < 3u ? clock_reconnect_failures : 2u;
    uint32_t delay = clock_backoff_ms[index];
    clock_reconnect_failures++;
    btstack_run_loop_set_timer_handler(&clock_reconnect_timer, clock_reconnect_timer_handler);
    btstack_run_loop_set_timer(&clock_reconnect_timer, delay);
    btstack_run_loop_add_timer(&clock_reconnect_timer);
    clock_reconnect_pending = true;
    printf("[BT] Reconnect scheduled in %lu ms\n", (unsigned long)delay);
}

static void a2dp_source_demo_start_scanning(void){
]=])
string(FIND "${_source}" "${_old_scan_function}" _scan_fn_position)
if (_scan_fn_position EQUAL -1)
    message(FATAL_ERROR "BTstack scan function changed; review direct-connect patch")
endif()
string(REPLACE "${_old_scan_function}" "${_new_scan_function}" _source "${_source}")

# At boot prefer a directly addressed, already paired sink.
set(_old_boot_scan [=[
            a2dp_source_demo_start_scanning();
]=])
set(_new_boot_scan [=[
            if (clock_autoconnect_target[0] != 0){
                printf("[BT] Boot direct connect target %s\n", clock_autoconnect_target);
                if (!clock_a2dp_connect_address(clock_autoconnect_target)){
                    printf("[BT] Direct start refused; falling back to scan\n");
                    a2dp_source_demo_start_scanning();
                }
            } else {
                a2dp_source_demo_start_scanning();
            }
]=])
string(FIND "${_source}" "${_old_boot_scan}" _boot_scan_pos)
if (_boot_scan_pos EQUAL -1)
    message(FATAL_ERROR "BTstack startup scan changed; review auto-connect patch")
endif()
string(REPLACE "${_old_boot_scan}" "${_new_boot_scan}" _source "${_source}")
if(NOT DEFINED AUTO_TARGET_ADDR)
    set(AUTO_TARGET_ADDR "")
endif()
# CMake regular expressions do not reliably support {n} repetitions.
# Validate exactly six hexadecimal octets, keeping the empty-target mode.
if(NOT AUTO_TARGET_ADDR STREQUAL "")
    string(REPLACE ":" ";" _addr_octets "${AUTO_TARGET_ADDR}")
    list(LENGTH _addr_octets _addr_count)
    if(NOT _addr_count EQUAL 6)
        message(FATAL_ERROR "Invalid AUTO_TARGET_ADDR: expected six hex octets")
    endif()
    foreach(_octet IN LISTS _addr_octets)
        if(NOT _octet MATCHES "^[0-9A-Fa-f][0-9A-Fa-f]$")
            message(FATAL_ERROR "Invalid AUTO_TARGET_ADDR: each octet must contain two hex digits")
        endif()
    endforeach()
endif()
string(REPLACE "__CLOCK_AUTO_TARGET__" "${AUTO_TARGET_ADDR}" _source "${_source}")

# Retrying an A2DP connection requires waiting for failure or release.
set(_old_failed_connection [=[
                media_tracker.a2dp_cid = 0;
                break;
]=])
set(_new_failed_connection [=[
                media_tracker.a2dp_cid = 0;
                clock_reconnect_schedule();
                break;
]=])
string(FIND "${_source}" "${_old_failed_connection}" _failed_position)
if(_failed_position EQUAL -1)
    message(FATAL_ERROR "BTstack connection-failure handler changed")
endif()
string(REPLACE "${_old_failed_connection}" "${_new_failed_connection}" _source "${_source}")

set(_old_connected [=[
            media_tracker.a2dp_cid = cid;
            media_tracker.volume = 32;
]=])
set(_new_connected [=[
            media_tracker.a2dp_cid = cid;
            clock_reconnect_cancel();
            clock_reconnect_failures = 0;
            media_tracker.volume = 32;
]=])
string(FIND "${_source}" "${_old_connected}" _connected_position)
if(_connected_position EQUAL -1)
    message(FATAL_ERROR "BTstack connection-success handler changed")
endif()
string(REPLACE "${_old_connected}" "${_new_connected}" _source "${_source}")

set(_old_signal_released [=[
                media_tracker.a2dp_cid = 0;
                printf("A2DP Source: Signaling released.\n\n");
]=])
set(_new_signal_released [=[
                media_tracker.a2dp_cid = 0;
                printf("A2DP Source: Signaling released.\n\n");
                clock_reconnect_schedule();
]=])
string(FIND "${_source}" "${_old_signal_released}" _signal_release_pos)
if(_signal_release_pos EQUAL -1)
    message(FATAL_ERROR "BTstack signaling-release handler changed")
endif()
string(REPLACE "${_old_signal_released}" "${_new_signal_released}" _source "${_source}")

set(_old_produce [=[
static void produce_audio(int16_t * pcm_buffer, int num_samples){
    btstack_audio_generator_generate(&audio_generator_state.generator.base, pcm_buffer, num_samples);
#ifdef VOLUME_REDUCTION
]=])
set(_new_produce [=[
/* Clock A2DP experiment: 300 ms tone followed by PCM silence. */
static uint32_t clock_tone_samples_remaining;
static bool clock_tone_finished_logged;
static bool clock_a2dp_stream_running;
static uint32_t clock_clip_frames_played;
static bool clock_clip_playing;
static bool clock_clip_finished_logged;
#define CLOCK_CLIP_RATE 8000u
#define CLOCK_CLIP_SAMPLES 3200u
// Short, flash-resident original two-note chime as signed 8-bit PCM.
// The sample data are not a synthesized oscillator in the A2DP callback.
#include "clock_pcm_clip.h"

bool clock_a2dp_play_clip(void){
    if (!clock_a2dp_stream_running || !audio_generator_state.initialized) return false;
    clock_tone_samples_remaining = 0;
    clock_tone_finished_logged = true;
    clock_clip_frames_played = 0;
    clock_clip_playing = true;
    clock_clip_finished_logged = false;
    printf("[PCM] Playing 400 ms flash-resident 8kHz PCM clip (resampled to %d Hz)\n",
           current_sample_rate);
    return true;
}

// Called only from the BTstack event loop. No threads or blocking calls.
bool clock_a2dp_play_tone(void){
    if (!clock_a2dp_stream_running || !audio_generator_state.initialized) return false;
    clock_clip_playing = false;
    clock_tone_samples_remaining = ((uint32_t) current_sample_rate * 300u) / 1000u;
    clock_tone_finished_logged = false;
    printf("[TONE] Replay requested: 300 ms\n");
    return true;
}

void clock_a2dp_stop_tone(void){
    clock_tone_samples_remaining = 0;
    clock_tone_finished_logged = true;
    clock_clip_playing = false;
    printf("[TONE] Stopped; sending silence\n");
}

void clock_a2dp_print_status(void){
    printf("[INFO] A2DP stream: %s; audio: %s; sample rate: %d Hz; samples remaining: %lu\n",
           clock_a2dp_stream_running ? "ACTIVE" : "INACTIVE",
           clock_clip_playing ? "PCM_CLIP" : (clock_tone_samples_remaining ? "TONE" : "SILENCE"),
           current_sample_rate, (unsigned long)clock_tone_samples_remaining);
}

static void produce_audio(int16_t * pcm_buffer, int num_samples){
    if (clock_clip_playing){
        int i = 0;
        while (i < num_samples && clock_clip_playing){
            uint32_t sample_position = (uint32_t)(((uint64_t)clock_clip_frames_played * CLOCK_CLIP_RATE) /
                                                  (uint32_t)current_sample_rate);
            if (sample_position >= CLOCK_CLIP_SAMPLES){
                clock_clip_playing = false;
                break;
            }
            // Signed 8-bit source PCM -> 16-bit stereo; nearest-neighbor resample.
            int16_t pcm = (int16_t)((int16_t)clock_pcm_clip[sample_position] * 256);
            pcm_buffer[i * NUM_CHANNELS] = pcm;
            pcm_buffer[i * NUM_CHANNELS + 1] = pcm;
            ++clock_clip_frames_played;
            ++i;
        }
        if (i < num_samples){
            memset(&pcm_buffer[i * NUM_CHANNELS], 0,
                   (size_t)(num_samples - i) * NUM_CHANNELS * sizeof(int16_t));
            if (!clock_clip_finished_logged){
                printf("[PCM] Clip complete; continuing SBC silence\n");
                clock_clip_finished_logged = true;
            }
        }
    } else {
        int tone_samples = num_samples;
        if ((uint32_t) tone_samples > clock_tone_samples_remaining){
            tone_samples = (int) clock_tone_samples_remaining;
        }
        if (tone_samples > 0){
            btstack_audio_generator_generate(&audio_generator_state.generator.base,
                                             pcm_buffer, tone_samples);
            clock_tone_samples_remaining -= (uint32_t) tone_samples;
        }
        if (tone_samples < num_samples){
            memset(&pcm_buffer[tone_samples * NUM_CHANNELS], 0,
                   (size_t)(num_samples - tone_samples) * NUM_CHANNELS * sizeof(int16_t));
            if (!clock_tone_finished_logged && clock_tone_samples_remaining == 0){
                printf("[TONE] 300 ms complete; sending SBC silence, A2DP stays active\n");
                clock_tone_finished_logged = true;
            }
        }
    }
#ifdef VOLUME_REDUCTION
]=])
string(FIND "${_source}" "${_old_produce}" _produce_position)
if (_produce_position EQUAL -1)
    message(FATAL_ERROR "BTstack produce_audio() changed; review upstream before patching")
endif()
string(REPLACE "${_old_produce}" "${_new_produce}" _source "${_source}")

set(_old_config [=[
    audio_generator_state.initialized = true;
    media_tracker.sbc_storage_count = 0;
    media_tracker.samples_ready = 0;
]=])
set(_new_config [=[
    audio_generator_state.initialized = true;
    clock_tone_samples_remaining = ((uint32_t) sample_rate * 300u) / 1000u;
    clock_tone_finished_logged = false;
    clock_clip_playing = false;
    printf("[TONE] Scheduled 300 ms at %d Hz (%lu samples per channel)\n",
           sample_rate, (unsigned long)clock_tone_samples_remaining);
    media_tracker.sbc_storage_count = 0;
    media_tracker.samples_ready = 0;
]=])
string(FIND "${_source}" "${_old_config}" _config_position)
if (_config_position EQUAL -1)
    message(FATAL_ERROR "BTstack audio-source initialization changed; review upstream before patching")
endif()
string(REPLACE "${_old_config}" "${_new_config}" _source "${_source}")


# Keep our API's stream state consistent with Bluetooth events.
set(_old_started [=[
            a2dp_demo_timer_start(&media_tracker);
]=])
set(_new_started [=[
            clock_a2dp_stream_running = true;
            a2dp_demo_timer_start(&media_tracker);
]=])
string(FIND "${_source}" "${_old_started}" _started_at)
if (_started_at EQUAL -1)
    message(FATAL_ERROR "BTstack stream-start handler changed")
endif()
string(REPLACE "${_old_started}" "${_new_started}" _source "${_source}")

set(_old_stop [=[
            a2dp_demo_timer_stop(&media_tracker);
            break;
]=])
set(_new_stop [=[
            clock_a2dp_stream_running = false;
            clock_tone_samples_remaining = 0;
            clock_clip_playing = false;
            a2dp_demo_timer_stop(&media_tracker);
            break;
]=])
# Same exact block is present in both SUSPENDED and RELEASED handlers.
string(FIND "${_source}" "${_old_stop}" _stop_at)
if (_stop_at EQUAL -1)
    message(FATAL_ERROR "BTstack stream-stop handler changed")
endif()
string(REPLACE "${_old_stop}" "${_new_stop}" _source "${_source}")

file(WRITE "${PATCHED_SOURCE}" "${_source}")
message(STATUS "Generated locally patched A2DP source: ${PATCHED_SOURCE}")
