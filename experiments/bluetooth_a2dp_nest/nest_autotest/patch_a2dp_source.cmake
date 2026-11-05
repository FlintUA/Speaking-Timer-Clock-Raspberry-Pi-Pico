# Generate a locally modified copy of the upstream BTstack A2DP example.
# The upstream file is not redistributed in this repository.
# Fail on SDK source changes instead of silently using an unmodified infinite sine.

if (NOT DEFINED UPSTREAM_SOURCE OR NOT DEFINED PATCHED_SOURCE)
    message(FATAL_ERROR "UPSTREAM_SOURCE and PATCHED_SOURCE are required")
endif()

file(READ "${UPSTREAM_SOURCE}" _source)

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

// Called only from the BTstack event loop. No threads or blocking calls.
bool clock_a2dp_play_tone(void){
    if (!clock_a2dp_stream_running || !audio_generator_state.initialized) return false;
    clock_tone_samples_remaining = ((uint32_t) current_sample_rate * 300u) / 1000u;
    clock_tone_finished_logged = false;
    printf("[TONE] Replay requested: 300 ms\n");
    return true;
}

void clock_a2dp_stop_tone(void){
    clock_tone_samples_remaining = 0;
    clock_tone_finished_logged = true;
    printf("[TONE] Stopped; sending silence\n");
}

void clock_a2dp_print_status(void){
    printf("[INFO] A2DP stream: %s; audio: %s; sample rate: %d Hz; samples remaining: %lu\n",
           clock_a2dp_stream_running ? "ACTIVE" : "INACTIVE",
           clock_tone_samples_remaining ? "TONE" : "SILENCE",
           current_sample_rate, (unsigned long)clock_tone_samples_remaining);
}

static void produce_audio(int16_t * pcm_buffer, int num_samples){
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
        if (!clock_tone_finished_logged){
            printf("[TONE] 300 ms complete; sending SBC silence, A2DP stays active\n");
            clock_tone_finished_logged = true;
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
