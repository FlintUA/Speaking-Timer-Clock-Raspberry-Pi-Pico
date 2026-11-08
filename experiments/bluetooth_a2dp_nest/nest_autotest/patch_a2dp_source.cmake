# Generate a locally modified copy of the upstream BTstack A2DP example.
# The upstream file is not redistributed in this repository.
# Fail on SDK source changes instead of silently using an unmodified infinite sine.

if (NOT DEFINED UPSTREAM_SOURCE OR NOT DEFINED PATCHED_SOURCE)
    message(FATAL_ERROR "UPSTREAM_SOURCE and PATCHED_SOURCE are required")
endif()

file(READ "${UPSTREAM_SOURCE}" _source)
string(PREPEND _source "#include \"clock_a2dp_control.h\"\n#include \"clock_bt_policy.h\"\n")

# Direct connect to an already known Bluetooth MAC without requiring inquiry mode.
# The upstream example only connects after a discoverable inquiry result.
# Keep the MAC outside firmware source; receive it over USB for this test.
set(_old_scan_function [=[
static void a2dp_source_demo_start_scanning(void){
]=])
set(_new_scan_function [=[
#include "clock_a2dp_control.h"
// Flash v5 overrides the legacy build-time default at boot.
static char clock_autoconnect_target[18] = "__CLOCK_AUTO_TARGET__";
static bool clock_bt_boot_held;
static bd_addr_t clock_connected_addr;
static bool clock_connected_addr_valid;
bool clock_a2dp_stream_matches(const uint8_t address[6]){
    return address && clock_a2dp_stream_active() &&
           clock_connected_addr_valid &&
           memcmp(clock_connected_addr, address, 6) == 0;
}
bool clock_a2dp_avrcp_available(void){ return media_tracker.avrcp_cid != 0; }
uint8_t clock_a2dp_volume(void){ return clock_bt_volume; }
uint8_t clock_a2dp_remote_volume(void){ return clock_bt_remote_volume; }
bool clock_a2dp_pcm_volume_fallback(void){ return clock_bt_pcm_fallback; }
void clock_a2dp_set_volume(uint8_t level){
    if (level > 30u) level = 30u;
    clock_bt_volume = level;
    if (!media_tracker.avrcp_cid){
        clock_bt_pcm_fallback = true;
        printf("[BT-VOL] %u/30 PCM fallback (AVRCP unavailable)\n", level);
        return;
    }
    uint8_t absolute = (uint8_t)(((uint16_t)level * 127u + 15u) / 30u);
    uint8_t status = avrcp_controller_set_absolute_volume(media_tracker.avrcp_cid,
                                                           absolute);
    clock_bt_pcm_fallback = status != ERROR_CODE_SUCCESS;
    printf("[BT-VOL] %u/30 AVRCP absolute=%u status=0x%02x mode=%s\n",
           level, absolute, status, clock_bt_pcm_fallback ? "PCM" : "AVRCP");
}

static bool clock_bt_stack_ready;
static bool clock_bt_scan_requested;
static bool clock_bt_scan_running;
static bool clock_bt_switch_requested;
static clock_bt_device_t clock_bt_found[CLOCK_BT_MAX_DEVICES];
static unsigned clock_bt_found_count;
// Original inquiry parameters are required by gap_remote_name_request().
static uint8_t clock_bt_scan_page_mode[CLOCK_BT_MAX_DEVICES];
static uint16_t clock_bt_scan_clock_offset[CLOCK_BT_MAX_DEVICES];
static uint8_t clock_bt_name_needed[CLOCK_BT_MAX_DEVICES];
static int clock_bt_name_pending = -1;
static void clock_bt_scan_names_next(void);
const char *clock_a2dp_target(void){ return clock_autoconnect_target; }
bool clock_a2dp_autoconnect_enabled(void){ return !clock_bt_boot_held && clock_autoconnect_target[0]!=0; }
unsigned clock_a2dp_scan_count(void){ return clock_bt_found_count; }
bool clock_a2dp_scan_active(void){
    return clock_bt_scan_requested || clock_bt_scan_running ||
           clock_bt_name_pending >= 0;
}
bool clock_a2dp_scan_get(unsigned index, clock_bt_device_t *out){
    if (index >= clock_bt_found_count || !out) return false;
    *out = clock_bt_found[index]; return true;
}
static void clock_bt_capture_inquiry(const uint8_t *packet){
    bd_addr_t addr;
    gap_event_inquiry_result_get_bd_addr(packet, addr);
    for (unsigned i=0;i<clock_bt_found_count;i++){
        bd_addr_t existing;
        sscanf_bd_addr(clock_bt_found[i].address, existing);
        if (!bd_addr_cmp(addr, existing)) {
            // Some peers send a name only in a subsequent inquiry result.
            if (clock_bt_name_needed[i] &&
                gap_event_inquiry_result_get_name_available(packet)){
                int len = gap_event_inquiry_result_get_name_len(packet);
                if (len > (int)sizeof(clock_bt_found[i].name) - 1)
                    len = sizeof(clock_bt_found[i].name) - 1;
                if (len > 0) {
                    memcpy(clock_bt_found[i].name,
                           gap_event_inquiry_result_get_name(packet), (size_t)len);
                    clock_bt_found[i].name[len] = 0;
                    clock_bt_name_needed[i] = 0;
                }
            }
            return;
        }
    }
    if (clock_bt_found_count >= CLOCK_BT_MAX_DEVICES) return;
    unsigned index = clock_bt_found_count++;
    clock_bt_device_t *d = &clock_bt_found[index];
    snprintf(d->address, sizeof(d->address), "%02X:%02X:%02X:%02X:%02X:%02X",
             addr[0],addr[1],addr[2],addr[3],addr[4],addr[5]);
    // Do not label an unknown device "BT 1": it is not an identity.
    snprintf(d->name, sizeof(d->name), "Unknown %02X:%02X",
             addr[4], addr[5]);
    d->rssi = gap_event_inquiry_result_get_rssi_available(packet)
         ? (int8_t)gap_event_inquiry_result_get_rssi(packet) : -127;
    clock_bt_scan_page_mode[index] =
        gap_event_inquiry_result_get_page_scan_repetition_mode(packet);
    clock_bt_scan_clock_offset[index] =
        gap_event_inquiry_result_get_clock_offset(packet);
    clock_bt_name_needed[index] = 1;
    if (gap_event_inquiry_result_get_name_available(packet)){
        int len = gap_event_inquiry_result_get_name_len(packet);
        if (len > (int)sizeof(d->name)-1) len = sizeof(d->name)-1;
        if (len > 0){
            memcpy(d->name, gap_event_inquiry_result_get_name(packet), (size_t)len);
            d->name[len] = 0;
            clock_bt_name_needed[index] = 0;
        }
    }
    printf("[BT-SCAN] %u %s name=%s rssi=%d name_source=%s\n",
           clock_bt_found_count, d->address, d->name, (int)d->rssi,
           clock_bt_name_needed[index] ? "PENDING" : "EIR");
}
static void clock_bt_scan_finish(void){
    clock_bt_scan_requested = false;
    clock_bt_name_pending = -1;
    printf("[BT-SCAN] COMPLETE devices=%u\n", clock_bt_found_count);
}
static void clock_bt_scan_names_next(void){
    if (!clock_bt_scan_requested) return;
    for (unsigned i=0; i<clock_bt_found_count; ++i){
        if (!clock_bt_name_needed[i]) continue;
        clock_bt_name_needed[i] = 0; // at most one request per scan
        clock_bt_name_pending = (int)i;
        bd_addr_t addr;
        sscanf_bd_addr(clock_bt_found[i].address, addr);
        uint16_t offset = clock_bt_scan_clock_offset[i];
        if (offset) offset |= 0x8000u; // valid clock offset
        uint8_t rc = gap_remote_name_request(
            addr, clock_bt_scan_page_mode[i], offset);
        if (rc == ERROR_CODE_SUCCESS){
            printf("[BT-NAME] Request %s\n", clock_bt_found[i].address);
            return;
        }
        clock_bt_name_pending = -1;
        printf("[BT-NAME] Request failed %s status=0x%02x\n",
               clock_bt_found[i].address, rc);
    }
    clock_bt_scan_finish();
}
static void clock_bt_scan_begin(void){
    if (!clock_bt_scan_requested || clock_bt_scan_running) return;
    int rc = gap_inquiry_start(8u); // 10.24 seconds
    if (rc != ERROR_CODE_SUCCESS){
        printf("[BT-SCAN] inquiry failed 0x%02x\n", rc);
        clock_bt_scan_requested = false;
        return;
    }
    clock_bt_scan_running = true;
    puts("[BT-SCAN] ACTIVE Classic inquiry 10s");
}

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
    if (clock_bt_boot_held || !clock_bt_target_accepts(clock_autoconnect_target,device_addr,
            clock_bt_scan_requested || clock_bt_scan_running)) {
        puts("[BT-POLICY] Outgoing connection blocked: no matching selected target");
        return false;
    }
    if (scan_active){
        scan_active = false;
        gap_inquiry_stop();
    }
    uint8_t status = a2dp_source_establish_stream(device_addr, &media_tracker.a2dp_cid);
    printf("[BT] Direct connect %s status=0x%02x\n", text, status);
    return status == ERROR_CODE_SUCCESS;
}

bool clock_a2dp_connect(const char *address){
    return clock_a2dp_select_target(address);
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

// Scanning deliberately disconnects the current sink before inquiry.
bool clock_a2dp_scan_start(void){
    if (!clock_bt_stack_ready || clock_bt_scan_requested ||
        clock_bt_switch_requested) return false;
    clock_a2dp_clear_target();
    puts("[BT-POLICY] auto=OFF reason=SCAN; explicit Connect required");
    clock_bt_found_count = 0;
    clock_bt_name_pending = -1;
    memset(clock_bt_name_needed, 0, sizeof(clock_bt_name_needed));
    clock_bt_scan_requested = true;
    if (scan_active){
        scan_active = false;
        gap_inquiry_stop(); // inquiry-complete event starts user scan
        return true;
    }
    if (media_tracker.a2dp_cid != 0){
        uint8_t rc = a2dp_source_disconnect(media_tracker.a2dp_cid);
        if (rc != ERROR_CODE_SUCCESS){
            printf("[BT-SCAN] disconnect rejected 0x%02x\n", rc);
            clock_bt_scan_requested = false;
            return false;
        }
        puts("[BT-SCAN] releasing A2DP before inquiry");
        return true;
    }
    clock_bt_scan_begin();
    return clock_bt_scan_running;
}
void clock_a2dp_scan_stop(void){
    clock_bt_scan_requested = false;
    // Any in-flight remote-name completion is ignored after cancellation.
    clock_bt_name_pending = -1;
    if (clock_bt_scan_running){
        clock_bt_scan_running = false;
        gap_inquiry_stop();
    }
    puts("[BT-SCAN] Stopped; connection policy unchanged");
}
void clock_a2dp_boot_hold(bool hold){
    clock_bt_boot_held=hold;
    if (hold) clock_reconnect_cancel();
    if (!clock_bt_stack_ready) return;
    gap_connectable_control(clock_a2dp_autoconnect_enabled()?1:0);
    if (!hold && clock_bt_retry_allowed(clock_autoconnect_target,
        clock_bt_scan_requested || clock_bt_scan_running,clock_bt_switch_requested,
        media_tracker.a2dp_cid!=0)) {
        if (!clock_a2dp_connect_address(clock_autoconnect_target)) clock_reconnect_schedule();
    }
}
bool clock_a2dp_has_link_key(const char *address){
    if (!address || strlen(address) != 17u) return false;
    bd_addr_t addr;
    link_key_t key;
    link_key_type_t type;
    sscanf_bd_addr(address, addr);
    return gap_get_link_key_for_bd_addr(addr, key, &type);
}
void clock_a2dp_clear_target(void){
    clock_reconnect_cancel();
    clock_autoconnect_target[0] = 0;
    clock_bt_switch_requested = false;
    clock_connected_addr_valid = false;
    clock_a2dp_stream_running = false;
    clock_a2dp_stop_tone();
    gap_connectable_control(0);
    if (media_tracker.avrcp_cid) avrcp_disconnect(media_tracker.avrcp_cid);
}
void clock_a2dp_disconnect(void){
    clock_a2dp_scan_stop();
    clock_a2dp_clear_target();
    puts("[BT-POLICY] auto=OFF reason=USER_DISCONNECT; pairing keys retained");
    if (media_tracker.a2dp_cid) {
        uint8_t rc=a2dp_source_disconnect(media_tracker.a2dp_cid);
        printf("[BT] Disconnect requested status=0x%02x\n",rc);
    }
}
bool clock_a2dp_forget(const char *address){
    if (!address || strlen(address) != 17u) return false;
    bd_addr_t addr;
    sscanf_bd_addr(address, addr);
    if (clock_autoconnect_target[0]) {
        bd_addr_t selected;
        sscanf_bd_addr(clock_autoconnect_target, selected);
        if (!bd_addr_cmp(addr, selected)) {
            if (media_tracker.a2dp_cid &&
                a2dp_source_disconnect(media_tracker.a2dp_cid) != ERROR_CODE_SUCCESS)
                return false;
            clock_a2dp_clear_target();
        }
    }
    gap_drop_link_key_for_bd_addr(addr);
    return true;
}
bool clock_a2dp_select_target(const char *address){
    if (!address || strlen(address)!=17u) return false;
    for (unsigned i=0;i<17u;i++){
        if (i%3u==2u){ if (address[i]!=':') return false; }
        else if (!((address[i]>='0' && address[i]<='9') ||
                   (address[i]>='a' && address[i]<='f') ||
                   (address[i]>='A' && address[i]<='F'))) return false;
    }
    if (clock_bt_scan_running) gap_inquiry_stop();
    clock_bt_scan_running = false;
    clock_bt_scan_requested = false;
    clock_bt_name_pending = -1;
    scan_active = false;
    clock_reconnect_cancel();
    char previous_target[18];
    memcpy(previous_target, clock_autoconnect_target, sizeof(previous_target));
    memcpy(clock_autoconnect_target, address, 18u);
    printf("[BT-POLICY] auto=ON reason=CONNECT target=%s\n", clock_autoconnect_target);
    gap_connectable_control(clock_a2dp_autoconnect_enabled()?1:0);
    if (!clock_bt_stack_ready || clock_bt_boot_held) return true;
    if (media_tracker.a2dp_cid != 0){
        clock_bt_switch_requested = true;
        uint8_t rc = a2dp_source_disconnect(media_tracker.a2dp_cid);
        if (rc != ERROR_CODE_SUCCESS){
            clock_bt_switch_requested = false;
            memcpy(clock_autoconnect_target, previous_target, sizeof(previous_target));
            gap_connectable_control(clock_a2dp_autoconnect_enabled()?1:0);
            printf("[BT] switch disconnect failed 0x%02x\n", rc);
            clock_reconnect_schedule();
            return false;
        }
        return true;
    }
    clock_bt_switch_requested = false;
    if (!clock_a2dp_connect_address(clock_autoconnect_target))
        clock_reconnect_schedule();
    return true;
}

static void clock_reconnect_timer_handler(btstack_timer_source_t *timer){
    (void)timer;
    clock_reconnect_pending = false;
    if (clock_bt_boot_held || !clock_bt_retry_allowed(clock_autoconnect_target,
            clock_bt_scan_requested || clock_bt_scan_running,clock_bt_switch_requested,
            media_tracker.a2dp_cid!=0)) return;
    printf("[BT] Reconnect attempt %u to %s\n",
           clock_reconnect_failures, clock_autoconnect_target);
    if (!clock_a2dp_connect_address(clock_autoconnect_target)){
        clock_reconnect_schedule();
    }
}

static void clock_reconnect_schedule(void){
    if (clock_bt_boot_held || clock_reconnect_pending || !clock_bt_retry_allowed(clock_autoconnect_target,
        clock_bt_scan_requested || clock_bt_scan_running,clock_bt_switch_requested,
        media_tracker.a2dp_cid!=0)) return;
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
                    printf("[BT] Boot sink unavailable; LOCAL fallback, retry later\n");
                    clock_reconnect_schedule();
                }
            } else {
                puts("[BT] No saved sink; select a discoverable speaker using SETTINGS > Bluetooth");
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
                if (cid && media_tracker.a2dp_cid && cid!=media_tracker.a2dp_cid) {
                    puts("[BT-POLICY] Ignoring stale connection failure");
                    break;
                }
                media_tracker.a2dp_cid = 0;
                clock_connected_addr_valid = false;
                if (clock_bt_scan_requested) clock_bt_scan_begin();
                else clock_reconnect_schedule();
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
            if (clock_bt_boot_held || !clock_bt_target_accepts(clock_autoconnect_target,address,
                    clock_bt_scan_requested || clock_bt_scan_running)) {
                if (media_tracker.a2dp_cid==cid) clock_connected_addr_valid=false;
                puts("[BT-POLICY] Reject signaling: disconnected/scanning or stale peer");
                a2dp_source_disconnect(cid);
                break;
            }
            media_tracker.a2dp_cid = cid;
            memcpy(clock_connected_addr, address, 6);
            clock_connected_addr_valid = true;
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
                clock_connected_addr_valid = false;
                printf("A2DP Source: Signaling released.\n\n");
                if (clock_bt_scan_requested) clock_bt_scan_begin();
                else if (clock_bt_switch_requested){
                    clock_bt_switch_requested = false;
                    if (!clock_a2dp_connect_address(clock_autoconnect_target))
                        clock_reconnect_schedule();
                } else {
                    if (clock_autoconnect_target[0]) puts("[BT-POLICY] reason=LINK_LOST; retry permitted");
                    clock_reconnect_schedule();
                }
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
// Independent BT volume 0..30. No unsolicited AVRCP volume changes on connect.
static uint8_t clock_bt_volume = 16u;
static bool clock_bt_pcm_fallback = true;
static uint8_t clock_bt_remote_volume = 255u;

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
// Cooperative PCM ring: the BTstack callback consumes only ready RAM samples.
// Never touch SD SPI or Helix from this callback.
#include "pcm_ring.h"

uint32_t clock_a2dp_current_rate(void){ return (uint32_t)current_sample_rate; }

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

// True only after the A2DP media stream has started. Do not equate a
// signaling-only connection with an audio-ready remote output.
bool clock_a2dp_stream_active(void){
    return clock_a2dp_stream_running && clock_a2dp_autoconnect_enabled();
}

void clock_a2dp_print_status(void){
    printf("[BT-POLICY] auto=%s target=%s\n",clock_a2dp_autoconnect_enabled()?"ON":"OFF",clock_a2dp_target());
    printf("[INFO] A2DP stream: %s; audio: %s; sample rate: %d Hz; samples remaining: %lu\n",
           clock_a2dp_stream_running ? "ACTIVE" : "INACTIVE",
           clock_pcm_ring_active() ? "MP3_PCM" : (clock_clip_playing ? "PCM_CLIP" : (clock_tone_samples_remaining ? "TONE" : "SILENCE")),
           current_sample_rate, (unsigned long)clock_tone_samples_remaining);
}

static void produce_audio(int16_t * pcm_buffer, int num_samples){
    if (clock_pcm_ring_active()){
        clock_pcm_ring_pop(pcm_buffer, (uint32_t)num_samples);
    } else if (clock_clip_playing){
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
    // Independent gains: MP3 playback 53.3%, test tones/clips 10%.
    // 53.3% gives 4/3 amplitude (+2.50 dB) compared with the previous 40% setting.
    // Apply the gain after ring consumption, before SBC encoding. Division
    // attenuates all signed 16-bit samples without clipping/overflow.
    const bool clock_mp3_gain = clock_pcm_ring_active();
    // AVRCP changes sink volume; otherwise adjust outgoing PCM. Limit
    // gain to unity to avoid overflowing int16_t samples.
    // Fallback level 16 retains original fixed MP3/test gain.
    uint32_t gain_num = clock_mp3_gain ? 8u : 1u;
    uint32_t gain_den = clock_mp3_gain ? 15u : 10u;
    if (clock_bt_pcm_fallback){
        gain_num *= clock_bt_volume;
        gain_den *= 16u;
    }
    if (gain_num > gain_den) gain_num = gain_den;
    for (int sample = 0; sample < num_samples * NUM_CHANNELS; ++sample){
        pcm_buffer[sample] = (int16_t)(((int32_t)pcm_buffer[sample] *
                                        (int32_t)gain_num) / (int32_t)gain_den);
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
            clock_pcm_ring_set_active(false);
            a2dp_demo_timer_stop(&media_tracker);
            break;
]=])
# Same exact block is present in both SUSPENDED and RELEASED handlers.
string(FIND "${_source}" "${_old_stop}" _stop_at)
if (_stop_at EQUAL -1)
    message(FATAL_ERROR "BTstack stream-stop handler changed")
endif()
string(REPLACE "${_old_stop}" "${_new_stop}" _source "${_source}")


# On-demand discoverable Classic device enumeration, bypass upstream's
# "connect first speaker" behavior while manually browsing.
set(_bt_new_inquiry [=[
        case GAP_EVENT_INQUIRY_RESULT:
            if (clock_bt_scan_running){
                clock_bt_capture_inquiry(packet);
                break;
            }
]=])
string(FIND "${_source}" "        case GAP_EVENT_INQUIRY_RESULT:" _bt_found)
if(_bt_found EQUAL -1)
    message(FATAL_ERROR "BTstack inquiry result anchor changed")
endif()
string(REPLACE "        case GAP_EVENT_INQUIRY_RESULT:" "${_bt_new_inquiry}" _source "${_source}")

set(_bt_new_inquiry_done [=[
        case GAP_EVENT_INQUIRY_COMPLETE:
            if (clock_bt_scan_running){
                clock_bt_scan_running = false;
                clock_bt_scan_names_next();
                break;
            }
            if (clock_bt_scan_requested){
                clock_bt_scan_begin();
                break;
            }
]=])
string(FIND "${_source}" "        case GAP_EVENT_INQUIRY_COMPLETE:" _bt_done_found)
if(_bt_done_found EQUAL -1)
    message(FATAL_ERROR "BTstack inquiry completion anchor changed")
endif()
string(REPLACE "        case GAP_EVENT_INQUIRY_COMPLETE:" "${_bt_new_inquiry_done}" _source "${_source}")

# Resolve missing Classic names asynchronously after inquiry.
# BTstack emits the completion on the same HCI packet handler.
set(_bt_name_complete [=[
        case HCI_EVENT_REMOTE_NAME_REQUEST_COMPLETE:
            if (clock_bt_name_pending >= 0){
                int index = clock_bt_name_pending;
                clock_bt_name_pending = -1;
                if (clock_bt_scan_requested &&
                    (unsigned)index < clock_bt_found_count){
                    bd_addr_t remote;
                    reverse_bd_addr(&packet[3], remote);
                    bd_addr_t expected;
                    sscanf_bd_addr(clock_bt_found[index].address, expected);
                    if (!bd_addr_cmp(remote, expected) && packet[2] == 0){
                        // HCI event data: status[2], address[3..8],
                        // NUL-terminated remote name[9..].
                        const char *received = (const char *)&packet[9];
                        size_t size = (size_t)packet[1] + 2u;
                        size_t room = size > 9u ? size - 9u : 0u;
                        size_t length = 0;
                        while (length < room && received[length]) ++length;
                        if (length >= sizeof(clock_bt_found[index].name))
                            length = sizeof(clock_bt_found[index].name) - 1u;
                        if (length){
                            memcpy(clock_bt_found[index].name, received, length);
                            clock_bt_found[index].name[length] = 0;
                            printf("[BT-NAME] Resolved %s = %s\\n",
                                   clock_bt_found[index].address,
                                   clock_bt_found[index].name);
                        }
                    } else {
                        printf("[BT-NAME] No remote name for %s status=0x%02x\\n",
                               clock_bt_found[index].address, packet[2]);
                    }
                    clock_bt_scan_names_next();
                }
                break;
            }
]=])
string(FIND "${_source}" "        case GAP_EVENT_INQUIRY_COMPLETE:" _bt_name_anchor)
if(_bt_name_anchor EQUAL -1)
    message(FATAL_ERROR "BTstack remote-name event anchor changed")
endif()
string(REPLACE "        case GAP_EVENT_INQUIRY_COMPLETE:"
               "${_bt_name_complete}        case GAP_EVENT_INQUIRY_COMPLETE:"
               _source "${_source}")

set(_bt_new_working [=[
        case  BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING){
                clock_bt_stack_ready = true;
                gap_connectable_control(clock_a2dp_autoconnect_enabled()?1:0);
            }
]=])
string(FIND "${_source}" "        case  BTSTACK_EVENT_STATE:" _bt_ready_found)
if(_bt_ready_found EQUAL -1)
    message(FATAL_ERROR "BTstack working state anchor changed")
endif()
string(REPLACE "        case  BTSTACK_EVENT_STATE:" "${_bt_new_working}" _source "${_source}")


# Suppress incoming paging before application settings have been loaded.
string(FIND "${_source}" "    gap_discoverable_control(1);" _paging_anchor)
if(_paging_anchor EQUAL -1)
    message(FATAL_ERROR "BTstack paging/discoverability initialization changed")
endif()
string(REPLACE "    gap_discoverable_control(1);"
    "    gap_discoverable_control(0);\n    gap_connectable_control(0);" _source "${_source}")
foreach(_event A2DP_SUBEVENT_STREAM_ESTABLISHED A2DP_SUBEVENT_STREAM_STARTED A2DP_SUBEVENT_STREAM_RECONFIGURED A2DP_SUBEVENT_STREAM_SUSPENDED A2DP_SUBEVENT_STREAM_RELEASED)
    set(_event_anchor "        case ${_event}:")
    string(FIND "${_source}" "${_event_anchor}" _event_pos)
    if(_event_pos EQUAL -1)
        message(FATAL_ERROR "BTstack stream event changed: ${_event}")
    endif()
    string(TOLOWER "${_event}" _event_getter)
    set(_stream_policy "")
    if(NOT _event STREQUAL "A2DP_SUBEVENT_STREAM_SUSPENDED" AND NOT _event STREQUAL "A2DP_SUBEVENT_STREAM_RELEASED")
        set(_stream_policy "
            if (clock_bt_boot_held || !clock_connected_addr_valid ||
                !clock_bt_target_accepts(clock_a2dp_target(),clock_connected_addr,false)) {
                puts(\"[BT-POLICY] Reject late stream event\");
                if (media_tracker.a2dp_cid) a2dp_source_disconnect(media_tracker.a2dp_cid);
                break;
            }")
    endif()
    string(REPLACE "${_event_anchor}" "${_event_anchor}
            cid = ${_event_getter}_get_a2dp_cid(packet);
            if (cid!=media_tracker.a2dp_cid) {
                puts(\"[BT-POLICY] Ignoring stale stream CID\");
                break;
            }${_stream_policy}" _source "${_source}")
endforeach()
string(REPLACE "Accepting Pairing - TODO: require actual user action" "Pairing restricted to explicitly selected sink" _source "${_source}")
set(_pair_old "            gap_ssp_confirmation_response(address);")
string(FIND "${_source}" "${_pair_old}" _pair_anchor)
if(_pair_anchor EQUAL -1)
    message(FATAL_ERROR "BTstack SSP confirmation handler changed")
endif()
string(REPLACE "${_pair_old}" "            if (!clock_bt_boot_held && clock_bt_target_accepts(clock_a2dp_target(),address,clock_a2dp_scan_active()))
                gap_ssp_confirmation_response(address);
            else gap_ssp_confirmation_negative(address);" _source "${_source}")

# Apply AVRCP callbacks to the upstream generated source. These do not
# overwrite the BTstack example in the SDK checkout.
set(_clock_avrcp_connect_old [=[
            media_tracker.avrcp_cid = local_cid;
            avrcp_subevent_connection_established_get_bd_addr(packet, event_addr);
]=])
set(_clock_avrcp_connect_new [=[
            avrcp_subevent_connection_established_get_bd_addr(packet, event_addr);
            if (clock_bt_boot_held || !clock_bt_target_accepts(clock_a2dp_target(),event_addr,clock_a2dp_scan_active())) {
                puts("[BT-POLICY] Reject AVRCP: no matching selected sink");
                avrcp_disconnect(local_cid);
                return;
            }
            media_tracker.avrcp_cid = local_cid;
            clock_bt_pcm_fallback = false;
            clock_bt_remote_volume = 255u;
]=])
string(FIND "${_source}" "${_clock_avrcp_connect_old}" _clock_avrcp_conn_at)
if(_clock_avrcp_conn_at EQUAL -1)
    message(FATAL_ERROR "BTstack AVRCP connected handler changed")
endif()
string(REPLACE "${_clock_avrcp_connect_old}" "${_clock_avrcp_connect_new}" _source "${_source}")

set(_clock_avrcp_release_old [=[
        case AVRCP_SUBEVENT_CONNECTION_RELEASED:
            printf("AVRCP Target: Disconnected, avrcp_cid 0x%02x\n", avrcp_subevent_connection_released_get_avrcp_cid(packet));
            media_tracker.avrcp_cid = 0;
            return;
]=])
set(_clock_avrcp_release_new [=[
        case AVRCP_SUBEVENT_CONNECTION_RELEASED:
            if (avrcp_subevent_connection_released_get_avrcp_cid(packet)!=media_tracker.avrcp_cid) return;
            printf("AVRCP Target: Disconnected, avrcp_cid 0x%02x\n", avrcp_subevent_connection_released_get_avrcp_cid(packet));
            media_tracker.avrcp_cid = 0;
            clock_bt_pcm_fallback = true;
            clock_bt_remote_volume = 255u;
            return;
]=])
string(FIND "${_source}" "${_clock_avrcp_release_old}" _clock_avrcp_rel_at)
if(_clock_avrcp_rel_at EQUAL -1)
    message(FATAL_ERROR "BTstack AVRCP release handler changed")
endif()
string(REPLACE "${_clock_avrcp_release_old}" "${_clock_avrcp_release_new}" _source "${_source}")

set(_clock_avrcp_notify_old [=[
        case AVRCP_SUBEVENT_NOTIFICATION_VOLUME_CHANGED:
            printf("AVRCP Controller: Notification Absolute Volume %d %%\n", avrcp_subevent_notification_volume_changed_get_absolute_volume(packet) * 100 / 127);
            break;
]=])
set(_clock_avrcp_notify_new [=[
        case AVRCP_SUBEVENT_NOTIFICATION_VOLUME_CHANGED:
            clock_bt_remote_volume =
                avrcp_subevent_notification_volume_changed_get_absolute_volume(packet);
            printf("AVRCP Controller: Notification Absolute Volume %d %%\n",
                   clock_bt_remote_volume * 100 / 127);
            break;
]=])
string(FIND "${_source}" "${_clock_avrcp_notify_old}" _clock_avrcp_not_at)
if(_clock_avrcp_not_at EQUAL -1)
    message(FATAL_ERROR "BTstack AVRCP volume notification handler changed")
endif()
string(REPLACE "${_clock_avrcp_notify_old}" "${_clock_avrcp_notify_new}" _source "${_source}")

file(WRITE "${PATCHED_SOURCE}" "${_source}")
message(STATUS "Generated locally patched A2DP source: ${PATCHED_SOURCE}")
