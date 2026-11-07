#include "bt_catalog.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    uint8_t address[6];
    assert(bt_address_parse("04:21:44:fa:a9:0e",address));
    assert(address[3]==0xfa && address[5]==0x0e);
    assert(!bt_address_parse("4:21:44:FA:A9:0E",address));
    assert(!bt_address_parse("04:21:44:FA:A9:GG",address));
    assert(!bt_name_usable("Unknown A9:0E"));
    assert(bt_name_usable("Unknown Artist"));
    assert(!bt_name_usable("04:21:44:FA:A9:0E"));
    assert(!strcmp(bt_preferred_name("Portable","Unknown A9:0E","MAC"),"Portable"));
    clock_bt_known_t known[2]={0};
    assert(bt_address_parse("04:21:44:FA:A9:0E",known[0].address));
    assert(bt_address_parse("AC:67:84:23:DB:97",known[1].address));
    strcpy(known[0].name,"Portable"); strcpy(known[1].name,"AC:67:84:23:DB:97");
    clock_bt_device_t scan[]={
        {"04:21:44:fa:a9:0e","Unknown A9:0E",-50},
        {"AC:67:84:23:DB:97","Nest",-70},
        {"AA:BB:CC:DD:EE:FF","Television",-90}
    }, out[BT_CATALOG_CAPACITY];
    assert(bt_catalog_build(known,2,scan,3,out,BT_CATALOG_CAPACITY)==3);
    assert(!strcmp(out[0].name,"Portable"));
    assert(!strcmp(out[1].name,"Nest"));
    assert(!strcmp(out[2].name,"Television"));
    assert(bt_catalog_build(known,2,scan,3,out,2)==2);
    assert(bt_catalog_build(known,0,scan,0,out,BT_CATALOG_CAPACITY)==0);
    puts("bt_catalog_test: PASS");
}
