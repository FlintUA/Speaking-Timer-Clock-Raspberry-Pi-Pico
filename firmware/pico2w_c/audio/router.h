#pragma once
#include <stdbool.h>

// The router is backend-agnostic: no Bluetooth stack is linked at this stage.
// LOCAL always remains an available safety fallback.
typedef enum {
    AUDIO_OUTPUT_LOCAL = 0,
    AUDIO_OUTPUT_BT_NEST,
    AUDIO_OUTPUT_BT_JBL
} audio_output_t;

typedef struct {
    audio_output_t requested;
    audio_output_t effective;
    bool bt_connected;
} audio_router_t;

void audio_router_init(audio_router_t *router);
bool audio_router_select(audio_router_t *router, audio_output_t requested);
void audio_router_bluetooth_state(audio_router_t *router, bool connected);
const char *audio_router_name(audio_output_t output);
