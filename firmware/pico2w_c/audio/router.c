#include "router.h"

static void update_effective(audio_router_t *router) {
    if (router->requested != AUDIO_OUTPUT_LOCAL && router->bt_connected)
        router->effective = router->requested;
    else
        router->effective = AUDIO_OUTPUT_LOCAL;
}

void audio_router_init(audio_router_t *router) {
    router->requested = AUDIO_OUTPUT_LOCAL;
    router->effective = AUDIO_OUTPUT_LOCAL;
    router->bt_connected = false;
}

bool audio_router_select(audio_router_t *router, audio_output_t requested) {
    if (requested != AUDIO_OUTPUT_LOCAL &&
        requested != AUDIO_OUTPUT_BT_NEST &&
        requested != AUDIO_OUTPUT_BT_JBL) return false;
    router->requested = requested;
    // For a future BT implementation, the caller must set connected=false
    // before switching remote peers to avoid routing to a stale connection.
    if (requested != AUDIO_OUTPUT_LOCAL) router->bt_connected = false;
    update_effective(router);
    return true;
}

void audio_router_bluetooth_state(audio_router_t *router, bool connected) {
    router->bt_connected = connected;
    update_effective(router);
}

const char *audio_router_name(audio_output_t output) {
    switch (output) {
        case AUDIO_OUTPUT_LOCAL: return "LOCAL";
        case AUDIO_OUTPUT_BT_NEST: return "BT";
        case AUDIO_OUTPUT_BT_JBL: return "BT";
        default: return "UNKNOWN";
    }
}
