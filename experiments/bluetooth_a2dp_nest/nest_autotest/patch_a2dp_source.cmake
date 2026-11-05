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

file(WRITE "${PATCHED_SOURCE}" "${_source}")
message(STATUS "Generated locally patched A2DP source: ${PATCHED_SOURCE}")
