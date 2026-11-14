#include "clock_time.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint64_t utc(unsigned year, unsigned month, unsigned day, unsigned hour) {
    ds1302_datetime_t d={.year=(uint16_t)year,.month=(uint8_t)month,
                         .day=(uint8_t)day,.hour=(uint8_t)hour};
    uint64_t epoch=0; assert(clock_datetime_to_utc(&d,&epoch)); return epoch;
}
int main(void) {
    ds1302_datetime_t d;
    assert(utc(2000,1,1,0)==946684800ULL);
    assert(clock_utc_to_datetime(utc(2024,2,29,12),&d));
    assert(d.year==2024 && d.month==2 && d.day==29 && d.weekday==4);
    for (unsigned year=2000;year<=2099;++year) {
        uint64_t epoch=utc(year,12,31,23);
        assert(clock_utc_to_datetime(epoch,&d));
        uint64_t roundtrip; assert(clock_datetime_to_utc(&d,&roundtrip));
        assert(roundtrip==epoch);
    }
    uint64_t spring=utc(2026,3,29,1), autumn=utc(2026,10,25,1);
    assert(clock_berlin_offset(spring-1)==3600);
    assert(clock_berlin_offset(spring)==7200);
    assert(clock_berlin_offset(autumn-1)==7200);
    assert(clock_berlin_offset(autumn)==3600);
    assert(clock_berlin_datetime(spring,&d) && d.hour==3);
    assert(clock_berlin_datetime(autumn,&d) && d.hour==2);
    clock_adjustment_t adjustment=clock_adjustment_plan(true,10000,13600,true);
    assert(adjustment.allow_boundary && adjustment.suppress_before==0);
    adjustment=clock_adjustment_plan(true,10000,13600,false);
    assert(!adjustment.allow_boundary); // Ordinary large sync: do not catch up.
    adjustment=clock_adjustment_plan(true,13600,10000,true);
    assert(adjustment.allow_boundary && adjustment.suppress_before==13620);
    adjustment=clock_adjustment_plan(false,0,13600,true);
    assert(!adjustment.allow_boundary);
    assert(clock_berlin_datetime(utc(2026,12,31,23),&d));
    assert(d.year==2027 && d.month==1 && d.day==1 && d.hour==0);
    uint8_t packet[48]={0}, nonce[8]={1,2,3,4,5,6,7,8};
    packet[0]=0x24; packet[1]=2; memcpy(packet+24,nonce,8);
    for (unsigned year=2026;year<=2040;year+=14) {
        uint64_t expected=utc(year,1,1,0);
        uint32_t ntp=(uint32_t)(expected+2208988800ULL);
        for (unsigned i=0;i<4;i++) packet[40+i]=(uint8_t)(ntp>>(24-8*i));
        uint64_t decoded=0; assert(clock_ntp_decode(packet,48,nonce,&decoded));
        assert(decoded==expected); // Includes the February 2036 NTP era rollover.
    }
    uint64_t epoch;
    assert(!clock_ntp_decode(packet,47,nonce,&epoch));
    packet[24]^=1; assert(!clock_ntp_decode(packet,48,nonce,&epoch)); packet[24]^=1;
    packet[0]=0xe4; assert(!clock_ntp_decode(packet,48,nonce,&epoch));
    packet[0]=0x24; packet[1]=0; assert(!clock_ntp_decode(packet,48,nonce,&epoch));
    packet[1]=16; assert(!clock_ntp_decode(packet,48,nonce,&epoch));
    packet[1]=2; packet[0]=0x23; assert(!clock_ntp_decode(packet,48,nonce,&epoch));
    ds1302_datetime_t civil={.year=2026,.month=3,.day=29,.hour=1,.minute=59,.second=59};
    assert(clock_berlin_local_valid(&civil));civil.hour=2;assert(!clock_berlin_local_valid(&civil));
    civil.hour=3;assert(clock_berlin_local_valid(&civil));
    civil.month=10;civil.day=25;civil.hour=2;assert(clock_berlin_local_valid(&civil));
    civil=(ds1302_datetime_t){.year=2028,.month=2,.day=29,.hour=12};assert(clock_berlin_local_valid(&civil));
    civil.year=2027;assert(!clock_berlin_local_valid(&civil));
    civil=(ds1302_datetime_t){.year=2000,.month=1,.day=1};assert(clock_berlin_local_valid(&civil));
    civil=(ds1302_datetime_t){.year=2099,.month=12,.day=31,.hour=23,.minute=59,.second=59};assert(clock_berlin_local_valid(&civil));
    civil.year=2100;assert(!clock_berlin_local_valid(&civil));
    puts("clock_time_test: PASS");
}
