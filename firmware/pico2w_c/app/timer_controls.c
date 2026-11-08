#include "timer_controls.h"

static uint8_t wrap(uint8_t value, uint8_t maximum, int direction) {
    if (direction > 0) return value >= maximum ? 0 : (uint8_t)(value+1);
    return value ? (uint8_t)(value-1) : maximum;
}
void timer_editor_open(timer_editor_t *e, uint32_t seconds) {
    if (seconds > 14400) seconds=14400;
    *e=(timer_editor_t){.hour=(uint8_t)(seconds/3600),
        .minute=(uint8_t)(seconds/60%60),.second=(uint8_t)(seconds%60)};
}
void timer_editor_rotate(timer_editor_t *e, int direction) {
    if (!direction) return;
    switch(e->field) {
        case TIMER_FIELD_HOUR: e->hour=wrap(e->hour,4,direction); break;
        case TIMER_FIELD_MINUTE: e->minute=wrap(e->minute,59,direction); break;
        case TIMER_FIELD_SECOND: e->second=wrap(e->second,59,direction); break;
    }
    // The countdown limit is exactly four hours, including editor drafts.
    if (e->hour==4) e->minute=e->second=0;
}
bool timer_editor_confirm(timer_editor_t *e, uint32_t *seconds) {
    if (e->field!=TIMER_FIELD_SECOND) { ++e->field; return false; }
    uint32_t value=(uint32_t)e->hour*3600+e->minute*60+e->second;
    *seconds=value ? value : 1;
    return true;
}
bool timer_editor_back(timer_editor_t *e) {
    if (e->field==TIMER_FIELD_HOUR) return true;
    --e->field;
    return false;
}
uint32_t timer_quick_adjust(timer_acceleration_t *s, uint32_t seconds,
                            int direction, uint64_t now_ms, unsigned *step) {
    unsigned increment=1;
    if (!direction) { if(step) *step=0; return seconds; }
    if (!s->valid || direction!=s->direction || now_ms<s->previous_ms ||
        now_ms-s->previous_ms>=260) s->streak=0;
    else {
        // Both legacy speed bands reach the same 1/5/10-minute thresholds.
        if(s->streak<4) ++s->streak;
    }
    if(s->streak>=4) increment=10;
    else if(s->streak>=2) increment=5;
    s->valid=true; s->previous_ms=now_ms; s->direction=direction;
    int minutes=(int)(seconds/60)+(direction>0?(int)increment:-(int)increment);
    if(minutes<1) minutes=1;
    if(minutes>240) minutes=240;
    if(step) *step=increment;
    return (uint32_t)minutes*60;
}
