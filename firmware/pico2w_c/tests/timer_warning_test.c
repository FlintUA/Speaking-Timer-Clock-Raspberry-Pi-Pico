#include <assert.h>
#include "timer_warning.h"
int main(void) {
 timer_warning_t s={0};uint64_t end=100000;
 assert(!timer_warning_poll(&s,true,end,94999,true,true));
 for(uint8_t n=5;n>=1;--n) {
  uint64_t now=end-(uint64_t)n*1000;
  assert(timer_warning_poll(&s,true,end,now,true,true)==n);
  for(unsigned k=1;k<1000;k+=7) assert(!timer_warning_poll(&s,true,end,now+k,true,true));
 }
 assert(!timer_warning_poll(&s,true,end,end,true,true));
 assert(!timer_warning_poll(&s,false,end,end-5000,true,true));
 assert(timer_warning_poll(&s,true,end,end-5000,true,true)==5); // Restart.
 timer_warning_reset(&s);
 assert(!timer_warning_poll(&s,true,end,end-5000,false,true));
 assert(!timer_warning_poll(&s,true,end,end-4999,true,true)); // Enabling is not a replay.
 assert(!timer_warning_poll(&s,true,end,end-4000,true,false));
 assert(!timer_warning_poll(&s,true,end,end-3999,true,true)); // Unmute is not a replay.
 assert(timer_warning_poll(&s,true,end,end-3000,true,true)==3);
 timer_warning_reset(&s);
 assert(!timer_warning_poll(&s,true,end,end-1800,true,true)); // Late edge skipped.
 assert(!timer_warning_poll(&s,true,end,end-1799,true,true));
 assert(timer_warning_poll(&s,true,end,end-1000,true,true)==1);
 assert(!timer_warning_poll(&s,true,end,end+1,true,true));
 end=UINT64_MAX;
 assert(timer_warning_poll(&s,true,end,end-5000,true,true)==5);
 assert(timer_warning_poll(&s,true,end,end-3850,true,true)==4); // 150 ms allowed.
 assert(!timer_warning_poll(&s,true,end,end-2849,true,true)); // 151 ms skipped.
 return 0;
}
