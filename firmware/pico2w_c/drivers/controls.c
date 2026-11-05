#include "controls.h"
#include "pico/stdlib.h"
#include <stdbool.h>

// Preserve the pin order used by library/rotary.py:
// Rotary(dt, clk) and MicroPython's 11->10 / 11->01 step detection.
typedef struct {
    uint8_t dt_pin, clk_pin;
    uint8_t previous;
    control_event_type_t type;
} encoder_t;

typedef struct {
    uint8_t pin;
    bool stable_pressed;
    bool raw_pressed;
    uint64_t changed_at;
} button_t;

static encoder_t encoders[] = {
    {14, 15, 0, CONTROL_VOLUME_ROTATE},
    {11, 10, 0, CONTROL_TIMER_ROTATE}
};
static button_t buttons[BUTTON_COUNT] = {
    {20, false, false, 0}, // Volume encoder push
    {19, false, false, 0}, // Timer encoder push
    {28, false, false, 0}, // Timer mode
    {21, false, false, 0}, // Alarm
    {22, false, false, 0}, // Mode
    {26, false, false, 0}, // Minus
    {27, false, false, 0}  // Settings / Plus
};

static uint8_t encoder_state(const encoder_t *enc) {
    return (uint8_t)((gpio_get(enc->dt_pin) << 1) | gpio_get(enc->clk_pin));
}

void controls_init(void) {
    for (unsigned i = 0; i < sizeof(encoders) / sizeof(encoders[0]); ++i) {
        gpio_init(encoders[i].dt_pin);
        gpio_set_dir(encoders[i].dt_pin, GPIO_IN);
        gpio_pull_up(encoders[i].dt_pin);
        gpio_init(encoders[i].clk_pin);
        gpio_set_dir(encoders[i].clk_pin, GPIO_IN);
        gpio_pull_up(encoders[i].clk_pin);
        encoders[i].previous = encoder_state(&encoders[i]);
    }
    for (unsigned i = 0; i < BUTTON_COUNT; ++i) {
        gpio_init(buttons[i].pin);
        gpio_set_dir(buttons[i].pin, GPIO_IN);
        gpio_pull_up(buttons[i].pin);
        buttons[i].raw_pressed = gpio_get(buttons[i].pin) == 0;
        buttons[i].stable_pressed = buttons[i].raw_pressed;
        buttons[i].changed_at = 0;
    }
}

void controls_poll(uint64_t now_ms, control_callback_t callback, void *context) {
    for (unsigned i = 0; i < sizeof(encoders) / sizeof(encoders[0]); ++i) {
        encoder_t *enc = &encoders[i];
        uint8_t previous = enc->previous;
        uint8_t current = encoder_state(enc);
        if (current == previous) continue;
        enc->previous = current;
        // Match the existing MicroPython Rotary driver. One event per detent
        // if the encoder rests at 11; other quadrature edges are not counted.
        int8_t direction = 0;
        if (previous == 3 && current == 2) direction = +1;
        else if (previous == 3 && current == 1) direction = -1;
        if (direction && callback) {
            control_event_t event = {.type = enc->type, .direction = direction};
            callback(&event, context);
        }
    }
    for (unsigned i = 0; i < BUTTON_COUNT; ++i) {
        button_t *button = &buttons[i];
        bool raw_pressed = gpio_get(button->pin) == 0;
        if (raw_pressed != button->raw_pressed) {
            button->raw_pressed = raw_pressed;
            button->changed_at = now_ms;
        }
        // 25 ms stable input, using monotonic uptime; no blocking debounce.
        if (raw_pressed != button->stable_pressed &&
            now_ms - button->changed_at >= 25u) {
            button->stable_pressed = raw_pressed;
            if (callback) {
                control_event_t event = {
                    .type = raw_pressed ? CONTROL_BUTTON_PRESS : CONTROL_BUTTON_RELEASE,
                    .button = (control_button_t)i
                };
                callback(&event, context);
            }
        }
    }
}

const char *controls_button_name(control_button_t button) {
    static const char *names[BUTTON_COUNT] = {
        "VOLUME", "TIMER", "TIMER_MODE", "ALARM",
        "MODE", "MINUS", "SETTINGS"
    };
    return (unsigned)button < BUTTON_COUNT ? names[button] : "UNKNOWN";
}
