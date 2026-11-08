#include <assert.h>
#include <string.h>
#include "clock_bt_policy.h"

int main(void) {
    const uint8_t portable[6]={0x04,0x21,0x44,0xfa,0xa9,0x0e};
    const uint8_t nest[6]={0xac,0x67,0x84,0x23,0xdb,0x97};
    char target[18]="04:21:44:FA:A9:0E";
    assert(clock_bt_target_accepts(target,portable,false));
    assert(!clock_bt_target_accepts(target,nest,false));
    assert(clock_bt_retry_allowed(target,false,false,false)); // Link lost.
    assert(!clock_bt_retry_allowed(target,false,false,true)); // Already establishing/connected.
    assert(!clock_bt_retry_allowed(target,false,true,false)); // Explicit switch owns release.
    assert(!clock_bt_retry_allowed(target,true,false,false));
    assert(!clock_bt_target_accepts(target,portable,true)); // Scan/name browsing.
    target[0]=0; // Manual disconnect and scan both clear the desired target.
    for(unsigned callback=0;callback<1000;++callback) {
        assert(!clock_bt_retry_allowed(target,false,false,false));
        assert(!clock_bt_target_accepts(target,portable,false));
        assert(!clock_bt_target_accepts(target,nest,false));
    }
    // Finishing/cancelling inquiry does not restore the target.
    assert(!clock_bt_target_accepts(target,portable,false));
    strcpy(target,"AC:67:84:23:DB:97"); // Explicit Connect to Nest.
    assert(!clock_bt_target_accepts(target,portable,false)); // Late old-peer callback.
    assert(clock_bt_target_accepts(target,nest,false));
    assert(clock_bt_retry_allowed(target,false,false,false));
    strcpy(target,"ac:67:84:23:db:97");
    assert(clock_bt_target_accepts(target,nest,false));
    assert(!clock_bt_target_accepts(0,nest,false));
    assert(!clock_bt_target_accepts("AC-67:84:23:DB:97",nest,false));
    assert(!clock_bt_target_accepts("AC:67:84:23:DB:9Z",nest,false));
    assert(!clock_bt_target_accepts("AC:67:84:23:DB:970",nest,false));
    assert(!clock_bt_retry_allowed("",false,false,false)); // Restart with persisted OFF.
    return 0;
}
