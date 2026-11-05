#include "btstack_config_common.h"

// The shared Pico example configuration enables interactive stdin,
// which disables the upstream A2DP demo's automatic inquiry path.
// This standalone test is intentionally unattended: scan on HCI_STATE_WORKING.
#ifdef HAVE_BTSTACK_STDIN
#undef HAVE_BTSTACK_STDIN
#endif
