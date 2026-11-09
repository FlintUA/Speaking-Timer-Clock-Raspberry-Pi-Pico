#pragma once
#include <stdbool.h>
#include <stdint.h>

enum {
    DISPLAY_LIGHT_OFF,
    DISPLAY_LIGHT_ON,
    DISPLAY_LIGHT_AUTO,
    DISPLAY_LIGHT_DAY_DEFAULT = 100,
    DISPLAY_LIGHT_NIGHT_DEFAULT = 20
};

typedef struct {
    uint8_t mode, day_percent, night_percent;
} display_light_t;

static inline bool display_light_valid(display_light_t light) {
    return light.mode <= DISPLAY_LIGHT_AUTO &&
           light.day_percent <= 100u && light.night_percent <= 100u;
}

static inline uint8_t display_light_level(display_light_t light, uint8_t hour) {
    if (light.mode == DISPLAY_LIGHT_OFF) return 0;
    if (light.mode == DISPLAY_LIGHT_AUTO && (hour >= 22u || hour < 7u))
        return light.night_percent; // Includes an unavailable RTC (hour 24).
    return light.day_percent;
}
