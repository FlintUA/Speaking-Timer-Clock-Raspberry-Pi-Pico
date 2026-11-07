#include <assert.h>
#include <string.h>
#include "../services/settings_store.c"

uint8_t test_flash[PICO_FLASH_SIZE_BYTES];
uint8_t *test_image_end=test_flash+4096;
static bool fail_write;
void flash_range_erase(uint32_t offset, size_t length) {
    assert(offset+length<=sizeof(test_flash));
    memset(test_flash+offset,0xff,length);
}
void flash_range_program(uint32_t offset, const uint8_t *data, size_t length) {
    assert(length%FLASH_PAGE_SIZE==0 && offset+length<=sizeof(test_flash));
    for (size_t i=0;i<length;i++) test_flash[offset+i]&=data[i];
}
int flash_safe_execute(void (*callback)(void *), void *arg, unsigned timeout) {
    (void)timeout;
    if (fail_write) return -1;
    callback(arg); return PICO_OK;
}
static void legacy(unsigned version) {
    settings_record_v8_t r={0};
    r.magic=RECORD_MAGIC; r.version=version; r.sequence=17;
    r.language=1; r.volume=18; r.bt_volume=12;
    r.quiet_start=22; r.quiet_end=7;
    memset(r.reserved,0xff,sizeof(r.reserved));
    memset(r.reserved_v5,0xff,sizeof(r.reserved_v5));
    memset(r.reserved_v6,0xff,sizeof(r.reserved_v6));
    memset(r.reserved_v7,0xff,sizeof(r.reserved_v7));
    for(unsigned i=0;i<CLOCK_ALARM_COUNT;i++) {
        r.alarms[i][1]=(uint8_t)(7+i); r.alarms[i][4]=1;
    }
    r.bt_target_addr[0]=4;
    r.bt_known_count=1; r.bt_known[0].address[0]=4;
    strcpy(r.bt_known[0].name,"Portable");
    size_t crc_offset=0;
    switch(version) {
        case 1:crc_offset=offsetof(settings_record_v1_t,crc32);break;
        case 2:crc_offset=offsetof(settings_record_v2_t,crc32);break;
        case 3:crc_offset=offsetof(settings_record_t,crc32);break;
        case 4:crc_offset=offsetof(settings_record_v4_t,crc32);break;
        case 5:crc_offset=offsetof(settings_record_v5_t,crc32);break;
        case 6:crc_offset=offsetof(settings_record_v6_t,crc32);break;
        case 7:crc_offset=offsetof(settings_record_v7_t,crc32);break;
        default:assert(false);
    }
    uint32_t crc=record_crc_bytes(&r,crc_offset);
    memcpy((uint8_t *)&r+crc_offset,&crc,sizeof(crc));
    memcpy(test_flash+LEGACY_SLOT_OFFSET,&r,record_size(&r));
}
int main(void) {
    for (unsigned v=1;v<=7;v++) {
        memset(test_flash,0xff,sizeof(test_flash)); legacy(v);
        clock_settings_t cfg;
        assert(settings_store_load(&cfg)==SETTINGS_STORE_OK);
        assert(cfg.language==1 && cfg.volume==18);
        assert(cfg.bt_volume==(v>=6?12:16));
        assert(cfg.bt_known_count==(v>=7?1:0));
        assert(!cfg.wifi_ssid[0] && !cfg.wifi_password[0]);
        assert(settings_store_save(&cfg)==SETTINGS_STORE_OK);
        assert(test_flash[LEGACY_SLOT_OFFSET]==0x4b); // Legacy sector retained.
        strcpy(cfg.wifi_ssid,"TestNetwork"); strcpy(cfg.wifi_password,"test-password");
        cfg.alarms[0].track=200;
        assert(settings_store_save(&cfg)==SETTINGS_STORE_OK);
        clock_settings_t loaded;
        assert(settings_store_load(&loaded)==SETTINGS_STORE_OK);
        assert(!strcmp(loaded.wifi_ssid,"TestNetwork"));
        assert(!strcmp(loaded.wifi_password,"test-password"));
        assert(loaded.alarms[0].track==200);
        fail_write=true;
        assert(settings_store_save(&cfg)==SETTINGS_STORE_FLASH_ERROR);
        fail_write=false;
        // Corrupt the newest slot: fall back to the previous valid A/B record.
        test_flash[SLOT1_OFFSET+20]^=1;
        assert(settings_store_load(&loaded)==SETTINGS_STORE_OK);
        assert(!loaded.wifi_ssid[0]);
        assert(settings_store_save(&cfg)==SETTINGS_STORE_OCCUPIED);
    }
    memset(test_flash,0xff,sizeof(test_flash));
    test_flash[SLOT0_OFFSET]=0x42;
    clock_settings_t cfg={.quiet_start=22,.quiet_end=7};
    alarm_defaults(cfg.alarms);
    assert(settings_store_save(&cfg)==SETTINGS_STORE_OCCUPIED);
    assert(test_flash[SLOT0_OFFSET]==0x42);
    puts("settings_store_test: PASS");
}
