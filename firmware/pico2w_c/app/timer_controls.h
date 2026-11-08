#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { TIMER_FIELD_HOUR, TIMER_FIELD_MINUTE, TIMER_FIELD_SECOND } timer_field_t;
typedef struct {
    uint8_t hour, minute, second;
    timer_field_t field;
} timer_editor_t;
typedef struct {
    uint64_t previous_ms;
    unsigned streak;
    int direction;
    bool valid;
} timer_acceleration_t;

void timer_editor_open(timer_editor_t *editor, uint32_t seconds);
void timer_editor_rotate(timer_editor_t *editor, int direction);
// True when the last field is confirmed; only then publish the draft duration.
bool timer_editor_confirm(timer_editor_t *editor, uint32_t *seconds);
// True when backing out of the first field cancels the editor.
bool timer_editor_back(timer_editor_t *editor);
uint32_t timer_quick_adjust(timer_acceleration_t *state, uint32_t seconds,
                            int direction, uint64_t now_ms, unsigned *step);
