#pragma once
#include <stdint.h>

typedef enum {
    CONTROL_VOLUME_ROTATE,
    CONTROL_TIMER_ROTATE,
    CONTROL_BUTTON_PRESS,
    CONTROL_BUTTON_RELEASE
} control_event_type_t;

typedef enum {
    BUTTON_VOLUME = 0,
    BUTTON_TIMER,
    BUTTON_TIMER_MODE,
    BUTTON_ALARM,
    BUTTON_MODE,
    BUTTON_MINUS,
    BUTTON_SETTINGS,
    BUTTON_COUNT
} control_button_t;

typedef struct {
    control_event_type_t type;
    int8_t direction; // +1 or -1 for encoder events
    control_button_t button;
} control_event_t;

typedef void (*control_callback_t)(const control_event_t *event, void *context);

void controls_init(void);
void controls_poll(uint64_t now_ms, control_callback_t callback, void *context);
const char *controls_button_name(control_button_t button);
