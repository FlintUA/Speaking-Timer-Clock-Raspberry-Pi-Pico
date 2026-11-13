#include "timer_warning.h"
void timer_warning_reset(timer_warning_t *s) {*s=(timer_warning_t){0};}
uint8_t timer_warning_poll(timer_warning_t *s,bool running,uint64_t deadline,
                           uint64_t now,bool enabled,bool sound_on) {
 if(!running || now>=deadline) {timer_warning_reset(s);return 0;}
 if(s->deadline!=deadline) {*s=(timer_warning_t){.deadline=deadline};}
 uint64_t left=deadline-now;
 if(left>5000u) return 0;
 uint8_t second=(uint8_t)((left+999u)/1000u);
 if(s->observed==second) return 0;
 s->observed=second;
 if(!enabled || !sound_on || (uint64_t)second*1000u-left>150u) return 0;
 return second;
}
