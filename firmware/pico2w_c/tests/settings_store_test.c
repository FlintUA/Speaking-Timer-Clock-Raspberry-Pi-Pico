#include <assert.h>
#include <string.h>
#include "../services/settings_store.c"

uint8_t test_flash[PICO_FLASH_SIZE_BYTES];
uint8_t *test_image_end=test_flash+4096;
static bool fail_write;
void flash_range_erase(uint32_t offset, size_t length) {
    assert(offset==SLOT0_OFFSET || offset==SLOT1_OFFSET);
    assert(offset+length<=sizeof(test_flash));
    memset(test_flash+offset,0xff,length);
}
void flash_range_program(uint32_t offset, const uint8_t *data, size_t length) {
    assert(offset==SLOT0_OFFSET || offset==SLOT1_OFFSET);
    assert(length%FLASH_PAGE_SIZE==0 && offset+length<=sizeof(test_flash));
    for (size_t i=0;i<length;i++) test_flash[offset+i]&=data[i];
}
int flash_safe_execute(void (*callback)(void *), void *arg, unsigned timeout) {
    (void)timeout;
    if (fail_write) return -1;
    callback(arg); return PICO_OK;
}
static void write_v11_fixture(settings_record_current_t *record, uint32_t offset) {
    assert(record->version == RECORD_V11);
    uint32_t crc = record_crc_bytes(record, RECORD_V11_CRC_OFFSET);
    memcpy((uint8_t *)record + RECORD_V11_CRC_OFFSET, &crc, sizeof(crc));
    memcpy(test_flash + offset, record, record_size(record));
}
static void legacy(unsigned version) {
    settings_record_current_t r={0};
    r.magic=RECORD_MAGIC; r.version=version; r.sequence=17;
    r.language=1; r.volume=18; r.bt_volume=12;
    r.quiet_start=22; r.quiet_end=7;
    r.audio_manual_jbl=r.audio_hourly_jbl=r.audio_timer_jbl=r.audio_alarm_jbl=1;
    memset(r.reserved,0xff,sizeof(r.reserved));
    memset(r.reserved_v5,0xff,sizeof(r.reserved_v5));
    memset(r.reserved_v6,0xff,sizeof(r.reserved_v6));
    memset(r.reserved_v7,0xff,sizeof(r.reserved_v7));
    memset(r.reserved_v8,0xff,sizeof(r.reserved_v8));
    for(unsigned i=0;i<CLOCK_ALARM_COUNT;i++) {
        r.alarms[i][1]=(uint8_t)(7+i); r.alarms[i][4]=1;
    }
    r.bt_target_addr[0]=4;
    r.bt_known_count=1; r.bt_known[0].address[0]=4;
    strcpy(r.bt_known[0].name,"Portable");
    if(version>=3) {
        r.alarms[0][0]=1; r.alarms[0][2]=15; r.alarms[0][3]=ALARM_SOUND_MUSIC;
        r.alarms[0][4]=version==8?200:2;
    }
    if(version==8) {
        strcpy(r.wifi_ssid,"SavedNetwork"); strcpy(r.wifi_password,"saved-password");
    }
    size_t crc_offset=0;
    switch(version) {
        case 1:crc_offset=offsetof(settings_record_v1_t,crc32);break;
        case 2:crc_offset=offsetof(settings_record_v2_t,crc32);break;
        case 3:crc_offset=offsetof(settings_record_t,crc32);break;
        case 4:crc_offset=offsetof(settings_record_v4_t,crc32);break;
        case 5:crc_offset=offsetof(settings_record_v5_t,crc32);break;
        case 6:crc_offset=offsetof(settings_record_v6_t,crc32);break;
        case 7:crc_offset=offsetof(settings_record_v7_t,crc32);break;
        case 8:crc_offset=offsetof(settings_record_v8_t,crc32);break;
        default:assert(false);
    }
    uint32_t crc=record_crc_bytes(&r,crc_offset);
    memcpy((uint8_t *)&r+crc_offset,&crc,sizeof(crc));
    memcpy(test_flash+LEGACY_SLOT_OFFSET,&r,record_size(&r));
}
int main(void) {
    for (unsigned v=1;v<=8;v++) {
        memset(test_flash,0xff,sizeof(test_flash)); legacy(v);
        clock_settings_t cfg;
        assert(settings_store_load(&cfg)==SETTINGS_STORE_OK);
        assert(cfg.language==1 && cfg.volume==18);
        assert(cfg.timer_voice==1 && cfg.timer_sound==0 && cfg.timer_track==1);
        assert(cfg.bt_volume==(v>=6?12:16));
        assert(cfg.bt_known_count==(v>=7?1:0));
        assert(cfg.audio_manual_jbl==(v>=4?1:0) && cfg.audio_timer_jbl==(v>=4?1:0));
        assert(cfg.audio_hourly_jbl==cfg.audio_manual_jbl && cfg.audio_alarm_jbl==cfg.audio_timer_jbl);
        assert(cfg.bt_target_addr[0]==(v>=5?4:0));
        if(v<8) assert(!cfg.wifi_ssid[0] && !cfg.wifi_password[0]);
        else assert(!strcmp(cfg.wifi_ssid,"SavedNetwork") && !strcmp(cfg.wifi_password,"saved-password"));
        for(unsigned i=0;i<CLOCK_ALARM_COUNT;++i) assert(cfg.alarms[i].weekdays==CLOCK_ALARM_DAYS_ALL);
        assert(cfg.alarms[0].enabled==(v>=3));
        if(v>=3) assert(cfg.alarms[0].minute==15 && cfg.alarms[0].sound==ALARM_SOUND_MUSIC);
        if(v==8) assert(cfg.alarms[0].track==200);
        assert(settings_store_save(&cfg)==SETTINGS_STORE_OK);
        assert(test_flash[LEGACY_SLOT_OFFSET]==0x4b); // Legacy sector retained.
        strcpy(cfg.wifi_ssid,"TestNetwork"); strcpy(cfg.wifi_password,"test-password");
        cfg.alarms[0].track=200;
        cfg.alarms[0].weekdays=0x15; // Monday/Wednesday/Friday.
        assert(settings_store_save(&cfg)==SETTINGS_STORE_OK);
        clock_settings_t loaded;
        assert(settings_store_load(&loaded)==SETTINGS_STORE_OK);
        assert(!strcmp(loaded.wifi_ssid,"TestNetwork"));
        assert(!strcmp(loaded.wifi_password,"test-password"));
        assert(loaded.alarms[0].track==200);
        assert(loaded.alarms[0].weekdays==0x15);
        assert(loaded.bt_known_count==cfg.bt_known_count && loaded.bt_volume==cfg.bt_volume);
        if(v>=7) assert(!strcmp(loaded.bt_known[0].name,"Portable"));
        assert(loaded.audio_manual_jbl==cfg.audio_manual_jbl && loaded.audio_timer_jbl==cfg.audio_timer_jbl);
        cfg.alarms[0].weekdays=0;
        assert(settings_store_save(&cfg)==SETTINGS_STORE_UNSUPPORTED);
        cfg.alarms[0].weekdays=0x15;
        fail_write=true;
        assert(settings_store_save(&cfg)==SETTINGS_STORE_FLASH_ERROR);
        fail_write=false;
        // Corrupt the newest slot: fall back to the previous valid A/B record.
        test_flash[SLOT1_OFFSET+20]^=1;
        assert(settings_store_load(&loaded)==SETTINGS_STORE_OK);
        if(v<8) assert(!loaded.wifi_ssid[0]);
        else assert(!strcmp(loaded.wifi_ssid,"SavedNetwork"));
        assert(loaded.alarms[0].weekdays==CLOCK_ALARM_DAYS_ALL);
        assert(settings_store_save(&cfg)==SETTINGS_STORE_OCCUPIED);
    }
    // The installed v8 uses A/B slots, not only the read-only legacy slot.
    memset(test_flash,0xff,sizeof(test_flash)); legacy(8);
    settings_record_v8_t old;
    memcpy(&old,test_flash+LEGACY_SLOT_OFFSET,sizeof(old));
    memcpy(test_flash+SLOT0_OFFSET,&old,sizeof(old));
    old.sequence++;
    old.crc32=record_crc_bytes(&old,offsetof(settings_record_v8_t,crc32));
    memcpy(test_flash+SLOT1_OFFSET,&old,sizeof(old));
    clock_settings_t migrated;
    assert(settings_store_load(&migrated)==SETTINGS_STORE_OK);
    assert(migrated.alarms[0].track==200 && migrated.alarms[0].weekdays==0x7f);
    assert(!strcmp(migrated.wifi_ssid,"SavedNetwork"));
    assert(settings_store_save(&migrated)==SETTINGS_STORE_OK);
    settings_record_current_t latest;
    memcpy(&latest,test_flash+SLOT0_OFFSET,sizeof(latest));
    assert(latest.version==11 && latest.sequence==old.sequence+1);
    assert(!memcmp(test_flash+SLOT1_OFFSET,&old,sizeof(old))); // Old fallback retained.
    assert(settings_store_load(&migrated)==SETTINGS_STORE_OK);
    migrated.alarms[0].weekdays=0x60;
    assert(settings_store_save(&migrated)==SETTINGS_STORE_OK);
    assert(settings_store_load(&migrated)==SETTINGS_STORE_OK && migrated.alarms[0].weekdays==0x60);
    memcpy(&latest,test_flash+SLOT1_OFFSET,sizeof(latest));
    for(unsigned invalid=0;invalid<=0x80;invalid+=0x80) {
        settings_record_current_t bad=latest;
        bad.alarm_weekdays[0]=(uint8_t)invalid;
        write_v11_fixture(&bad, SLOT1_OFFSET);
        assert(settings_store_load(&migrated)==SETTINGS_STORE_OK);
        assert(migrated.alarms[0].weekdays==CLOCK_ALARM_DAYS_ALL);
        assert(settings_store_save(&migrated)==SETTINGS_STORE_OCCUPIED);
    }
    // Installed v9 migration: preserve weekdays, Wi-Fi, speaker registry and volumes.
    settings_record_v9_t v9;
    memcpy(&v9,&latest,sizeof(v9));
    v9.version=9; v9.sequence=200;
    v9.alarms[0][0]=1; v9.alarm_weekdays[0]=0x60;
    v9.crc32=record_crc_bytes(&v9,offsetof(settings_record_v9_t,crc32));
    memset(test_flash,0xff,sizeof(test_flash));
    memcpy(test_flash+SLOT0_OFFSET,&v9,sizeof(v9));
    settings_record_v9_t older_v9=v9;
    older_v9.sequence--;
    older_v9.crc32=record_crc_bytes(&older_v9,offsetof(settings_record_v9_t,crc32));
    memcpy(test_flash+SLOT1_OFFSET,&older_v9,sizeof(older_v9));
    assert(settings_store_load(&migrated)==SETTINGS_STORE_OK);
    assert(migrated.alarms[0].weekdays==0x60 && migrated.alarms[0].enabled);
    for(unsigned i=0;i<CLOCK_BIRTHDAY_COUNT;++i)
        assert(!migrated.birthdays[i].enabled && birthday_valid(&migrated.birthdays[i]));
    assert(!memcmp(test_flash+SLOT0_OFFSET,&v9,sizeof(v9))); // Load never writes.
    migrated.birthdays[0]=(birthday_t){.enabled=true,.month=2,.day=29,.hour=9,.minute=12,.last_year=2024};
    assert(settings_store_save(&migrated)==SETTINGS_STORE_OK);
    assert(!memcmp(test_flash+SLOT0_OFFSET,&v9,sizeof(v9))); // v9 remains fallback.
    clock_settings_t roundtrip;
    assert(settings_store_load(&roundtrip)==SETTINGS_STORE_OK);
    assert(roundtrip.birthdays[0].enabled && roundtrip.birthdays[0].day==29 && roundtrip.birthdays[0].last_year==2024);
    assert(roundtrip.alarms[0].weekdays==0x60);
    assert(!strcmp(roundtrip.wifi_ssid,migrated.wifi_ssid));
    assert(!strcmp(roundtrip.wifi_password,migrated.wifi_password));
    assert(roundtrip.bt_volume==migrated.bt_volume && roundtrip.volume==migrated.volume);
    assert(!memcmp(roundtrip.bt_known,migrated.bt_known,sizeof(roundtrip.bt_known)));
    // Disconnect/scan use the existing explicit-no-target marker, retaining bonds and every other setting.
    clock_settings_t disabled=roundtrip;
    memset(disabled.bt_target_addr,0xff,sizeof(disabled.bt_target_addr));
    assert(settings_store_save(&disabled)==SETTINGS_STORE_OK);
    clock_settings_t off;
    assert(settings_store_load(&off)==SETTINGS_STORE_OK);
    for(unsigned i=0;i<6;++i) assert(off.bt_target_addr[i]==0xff);
    assert(off.bt_known_count==roundtrip.bt_known_count);
    assert(!memcmp(off.bt_known,roundtrip.bt_known,sizeof(off.bt_known)));
    assert(off.volume==roundtrip.volume && off.bt_volume==roundtrip.bt_volume);
    assert(off.audio_manual_jbl==roundtrip.audio_manual_jbl && off.audio_alarm_jbl==roundtrip.audio_alarm_jbl);
    assert(!strcmp(off.wifi_ssid,roundtrip.wifi_ssid) && !strcmp(off.wifi_password,roundtrip.wifi_password));
    assert(off.alarms[0].weekdays==roundtrip.alarms[0].weekdays);
    assert(off.birthdays[0].day==29 && off.birthdays[0].last_year==2024);
    // Restore the previous fixture layout before the independent corruption test.
    memset(test_flash,0xff,sizeof(test_flash));
    memcpy(test_flash+SLOT0_OFFSET,&v9,sizeof(v9));
    assert(settings_store_save(&roundtrip)==SETTINGS_STORE_OK);
    migrated.birthdays[0].month=4; migrated.birthdays[0].day=31;
    assert(settings_store_save(&migrated)==SETTINGS_STORE_UNSUPPORTED);
    settings_record_current_t corrupt;
    memcpy(&corrupt,test_flash+SLOT1_OFFSET,sizeof(corrupt));
    corrupt.birthdays[0][2]=32;
    write_v11_fixture(&corrupt, SLOT1_OFFSET);
    assert(settings_store_load(&roundtrip)==SETTINGS_STORE_OK && !roundtrip.birthdays[0].enabled);
    assert(settings_store_save(&roundtrip)==SETTINGS_STORE_OCCUPIED); // Even valid CRC cannot authorize invalid content.
    // A v10 A/B record has its checksum at the v11 illumination field.
    memset(test_flash,0xff,sizeof(test_flash));
    assert(settings_store_save(&off)==SETTINGS_STORE_OK);
    memcpy(&latest,test_flash+SLOT0_OFFSET,sizeof(latest));
    latest.version=RECORD_V10;
    uint32_t old_crc=record_crc_bytes(&latest,RECORD_V10_CRC_OFFSET);
    memcpy((uint8_t *)&latest+RECORD_V10_CRC_OFFSET,&old_crc,sizeof(old_crc));
    memset(test_flash+SLOT0_OFFSET,0xff,FLASH_SECTOR_SIZE);
    memcpy(test_flash+SLOT0_OFFSET,&latest,record_size(&latest));
    assert(settings_store_load(&roundtrip)==SETTINGS_STORE_OK);
    assert(roundtrip.display_light.mode==DISPLAY_LIGHT_OFF);
    assert(roundtrip.display_light.day_percent==100 && roundtrip.display_light.night_percent==20);
    assert(roundtrip.birthdays[0].day==29 && roundtrip.birthdays[0].last_year==2024);
    assert(roundtrip.alarms[0].weekdays==0x60);
    assert(!memcmp(roundtrip.bt_known,off.bt_known,sizeof(off.bt_known)));
    assert(!memcmp(roundtrip.bt_target_addr,off.bt_target_addr,sizeof(off.bt_target_addr)));
    assert(roundtrip.volume==off.volume && roundtrip.bt_volume==off.bt_volume);
    assert(!strcmp(roundtrip.wifi_ssid,off.wifi_ssid) && !strcmp(roundtrip.wifi_password,off.wifi_password));
    assert(!memcmp(test_flash+SLOT0_OFFSET,&latest,record_size(&latest))); // Loading never migrates.
    assert(display_light_level(roundtrip.display_light,12)==0);
    roundtrip.display_light=(display_light_t){DISPLAY_LIGHT_AUTO,75,15};
    assert(settings_store_save(&roundtrip)==SETTINGS_STORE_OK);
    assert(settings_store_load(&roundtrip)==SETTINGS_STORE_OK);
    assert(roundtrip.display_light.mode==DISPLAY_LIGHT_AUTO);
    assert(display_light_level(roundtrip.display_light,6)==15);
    assert(display_light_level(roundtrip.display_light,7)==75);
    assert(display_light_level(roundtrip.display_light,21)==75);
    assert(display_light_level(roundtrip.display_light,22)==15);
    assert(display_light_level(roundtrip.display_light,24)==15);
    roundtrip.display_light.mode=DISPLAY_LIGHT_ON;
    assert(display_light_level(roundtrip.display_light,22)==75);
    roundtrip.display_light.day_percent=101;
    assert(settings_store_save(&roundtrip)==SETTINGS_STORE_UNSUPPORTED);
    memcpy(&corrupt,test_flash+SLOT1_OFFSET,sizeof(corrupt));
    corrupt.display_light.mode=3;
    write_v11_fixture(&corrupt, SLOT1_OFFSET);
    assert(settings_store_load(&roundtrip)==SETTINGS_STORE_OK);
    assert(roundtrip.display_light.mode==DISPLAY_LIGHT_OFF);
    assert(settings_store_save(&roundtrip)==SETTINGS_STORE_OCCUPIED);
    // Existing v11 settings survive adding phone credentials; no migration on load.
    memset(test_flash, 0xff, sizeof(test_flash));
    assert(settings_store_save(&off) == SETTINGS_STORE_OK);
    assert(settings_store_load(&roundtrip) == SETTINGS_STORE_OK);
    assert(!roundtrip.phone.home_trusted && !roundtrip.phone.password[0]);
    assert(!roundtrip.phone.browsers[0].scope);
    roundtrip.phone.home_trusted = 1;
    strcpy(roundtrip.phone.password, "0123456789abcdef");
    roundtrip.phone.browsers[0].scope = PHONE_SCOPE_DIRECT;
    strcpy(roundtrip.phone.browsers[0].token, "0123456789abcdef0123456789abcdef");
    assert(settings_store_save(&roundtrip) == SETTINGS_STORE_OK);
    memcpy(&latest, test_flash + SLOT1_OFFSET, sizeof(latest));
    assert(latest.version == RECORD_V12 && sector_owned(0) && sector_owned(1));
    assert(settings_store_load(&off) == SETTINGS_STORE_OK);
    assert(!memcmp(&off.phone, &roundtrip.phone, sizeof(off.phone)));
    assert(off.volume == roundtrip.volume && off.birthdays[0].day == 29);
    assert(!strcmp(off.wifi_password, roundtrip.wifi_password));
    off.phone.browsers[0].scope = 3;
    assert(settings_store_save(&off) == SETTINGS_STORE_UNSUPPORTED);
    off = roundtrip;
    off.phone.browsers[0] = (phone_browser_t){0};
    assert(settings_store_save(&off) == SETTINGS_STORE_OK);
    assert(settings_store_load(&roundtrip) == SETTINGS_STORE_OK);
    assert(!roundtrip.phone.browsers[0].scope && roundtrip.phone.home_trusted);
    // Installed v12 records load without writing and receive an unknown birth year.
    uint8_t old_slot[FLASH_SECTOR_SIZE];
    memcpy(old_slot, test_flash + SLOT0_OFFSET, sizeof(old_slot));
    assert(settings_store_load(&roundtrip)==SETTINGS_STORE_OK);
    for (unsigned i=0; i<CLOCK_BIRTHDAY_COUNT; ++i) assert(roundtrip.birthdays[i].birth_year==0);
    assert(!memcmp(old_slot, test_flash + SLOT0_OFFSET, sizeof(old_slot)));
    roundtrip.birthdays[0].birth_year=1984; // Existing February 29 remains valid.
    roundtrip.birthdays[1].birth_year=1985;
    roundtrip.birthdays[0].last_year=2024;
    assert(settings_store_save(&roundtrip)==SETTINGS_STORE_OK);
    assert(settings_store_load(&off)==SETTINGS_STORE_OK);
    assert(off.birthdays[0].birth_year==1984 && off.birthdays[1].birth_year==1985);
    assert(off.birthdays[0].last_year==2024 && off.birthdays[2].birth_year==0);
    assert(!memcmp(&off.phone,&roundtrip.phone,sizeof(off.phone)));
    assert(!strcmp(off.wifi_password,roundtrip.wifi_password));
    assert(off.alarms[0].weekdays==roundtrip.alarms[0].weekdays);
    assert(off.volume==roundtrip.volume && off.bt_volume==roundtrip.bt_volume);
    assert(off.display_light.mode==roundtrip.display_light.mode);
    settings_record_current_t with_year;
    memcpy(&with_year,test_flash+SLOT1_OFFSET,sizeof(with_year));
    assert(with_year.version==RECORD_V13 && sizeof(with_year)==512);
    off.birthdays[0].birth_year=1985;
    assert(settings_store_save(&off)==SETTINGS_STORE_UNSUPPORTED);
    test_flash[SLOT1_OFFSET+offsetof(settings_record_current_t,birth_years)]^=1;
    assert(settings_store_load(&off)==SETTINGS_STORE_OK);
    assert(off.birthdays[0].birth_year==0 && off.phone.home_trusted);
    assert(settings_store_save(&roundtrip)==SETTINGS_STORE_OCCUPIED);
    // A v13 record gains ten disabled slots in RAM without changing Flash on load.
    memset(test_flash, 0xff, sizeof(test_flash));
    assert(settings_store_save(&roundtrip) == SETTINGS_STORE_OK);
    uint8_t old_v13_slot[FLASH_SECTOR_SIZE];
    memcpy(old_v13_slot, test_flash + SLOT0_OFFSET, sizeof(old_v13_slot));
    assert(settings_store_load(&off) == SETTINGS_STORE_OK);
    for (unsigned i = STORED_BIRTHDAY_COUNT; i < CLOCK_BIRTHDAY_COUNT; ++i)
        assert(!off.birthdays[i].enabled && off.birthdays[i].month == 1u &&
               off.birthdays[i].day == 1u && off.birthdays[i].hour == 9u);
    assert(!memcmp(old_v13_slot, test_flash + SLOT0_OFFSET, sizeof(old_v13_slot)));
    off.birthdays[5] = (birthday_t){.enabled = true, .month = 3, .day = 9,
        .hour = 12, .minute = 30, .last_year = 2025, .birth_year = 1984};
    off.birthdays[14] = (birthday_t){.enabled = true, .month = 12, .day = 31,
        .hour = 23, .minute = 59, .last_year = 2024};
    fail_write = true;
    assert(settings_store_save(&off) == SETTINGS_STORE_FLASH_ERROR);
    fail_write = false;
    clock_settings_t before_v14;
    assert(settings_store_load(&before_v14) == SETTINGS_STORE_OK);
    assert(!before_v14.birthdays[5].enabled &&
           !memcmp(old_v13_slot, test_flash + SLOT0_OFFSET, sizeof(old_v13_slot)));
    assert(settings_store_save(&off) == SETTINGS_STORE_OK);
    settings_record_v14_t v14;
    memcpy(&v14, test_flash + SLOT1_OFFSET, sizeof(v14));
    assert(v14.prefix.version == RECORD_V14 && sizeof(v14) == 616u);
    assert(!memcmp(old_v13_slot, test_flash + SLOT0_OFFSET, sizeof(old_v13_slot)));
    clock_settings_t expanded;
    assert(settings_store_load(&expanded) == SETTINGS_STORE_OK);
    assert(expanded.birthdays[0].birth_year == off.birthdays[0].birth_year);
    assert(expanded.birthdays[5].birth_year == 1984 && expanded.birthdays[5].last_year == 2025);
    assert(expanded.birthdays[14].enabled && expanded.birthdays[14].minute == 59);
    assert(!memcmp(&expanded.phone, &off.phone, sizeof(expanded.phone)));
    assert(!memcmp(expanded.bt_known, off.bt_known, sizeof(expanded.bt_known)));
    test_flash[SLOT1_OFFSET + offsetof(settings_record_v14_t, birthdays) + 1u] ^= 1u;
    assert(settings_store_load(&expanded) == SETTINGS_STORE_OK);
    assert(!expanded.birthdays[5].enabled && expanded.birthdays[0].birth_year == off.birthdays[0].birth_year);
    assert(settings_store_save(&off) == SETTINGS_STORE_OCCUPIED);
    // v15 writes are conditional. v14 loads remain read-only and retain all data.
    memset(test_flash,0xff,sizeof(test_flash));
    assert(settings_store_save(&off)==SETTINGS_STORE_OK);
    assert(settings_store_load(&expanded)==SETTINGS_STORE_OK);
    assert(expanded.timer_voice==1 && expanded.timer_sound==0 && expanded.timer_track==1);
    uint8_t before_timer[FLASH_SECTOR_SIZE];
    memcpy(before_timer,test_flash+SLOT0_OFFSET,sizeof(before_timer));
    off.timer_sound=1; off.timer_track=255; off.timer_voice=0;
    fail_write=true;
    assert(settings_store_save(&off)==SETTINGS_STORE_FLASH_ERROR);
    fail_write=false;
    assert(settings_store_load(&expanded)==SETTINGS_STORE_OK && expanded.timer_voice==1);
    assert(settings_store_save(&off)==SETTINGS_STORE_OK);
    settings_record_v15_t v15;
    memcpy(&v15,test_flash+SLOT1_OFFSET,sizeof(v15));
    assert(v15.prefix.prefix.version==RECORD_VERSION && sizeof(v15)==624);
    assert(record_v15_valid(test_flash+SLOT1_OFFSET));
    assert(!memcmp(before_timer,test_flash+SLOT0_OFFSET,sizeof(before_timer)));
    assert(settings_store_load(&expanded)==SETTINGS_STORE_OK);
    assert(expanded.timer_voice==0 && expanded.timer_sound==1 && expanded.timer_track==255);
    assert(expanded.birthdays[5].birth_year==1984 && expanded.birthdays[14].enabled);
    assert(!memcmp(&expanded.phone,&off.phone,sizeof(off.phone)));
    assert(!memcmp(expanded.bt_known,off.bt_known,sizeof(off.bt_known)));
    assert(!strcmp(expanded.wifi_password,off.wifi_password));
    // Changing only message/sound selection round-trips, including fully silent.
    expanded.timer_sound=2; expanded.timer_voice=1;
    assert(settings_store_save(&expanded)==SETTINGS_STORE_OK);
    assert(settings_store_load(&off)==SETTINGS_STORE_OK && off.timer_sound==2 && off.timer_voice==1);
    off.timer_voice=0;
    assert(settings_store_save(&off)==SETTINGS_STORE_OK);
    assert(settings_store_load(&expanded)==SETTINGS_STORE_OK && expanded.timer_sound==2 && !expanded.timer_voice);
    off.timer_voice=2;
    assert(settings_store_save(&off)==SETTINGS_STORE_UNSUPPORTED);
    off.timer_voice=0;off.timer_sound=3;
    assert(settings_store_save(&off)==SETTINGS_STORE_UNSUPPORTED);
    off.timer_sound=1;off.timer_track=0;
    assert(settings_store_save(&off)==SETTINGS_STORE_UNSUPPORTED);
    // Valid CRC cannot make an invalid timer mode into an owned record.
    unsigned latest_timer; settings_record_current_t timer_prefix;
    assert(newest_valid(&latest_timer,&timer_prefix));
    uint32_t timer_offset=latest_timer ? SLOT1_OFFSET:SLOT0_OFFSET;
    memcpy(&v15,test_flash+timer_offset,sizeof(v15));
    v15.timer_sound=3;v15.crc32=record_crc_bytes(&v15,offsetof(settings_record_v15_t,crc32));
    memcpy(test_flash+timer_offset,&v15,sizeof(v15));
    assert(!sector_owned(latest_timer));
    assert(settings_store_load(&off)==SETTINGS_STORE_OK); // Previous A/B version survives.
    assert(settings_store_save(&off)==SETTINGS_STORE_OCCUPIED);
    memset(test_flash,0xff,sizeof(test_flash));
    test_flash[SLOT0_OFFSET]=0x42;
    clock_settings_t cfg={.quiet_start=22,.quiet_end=7,.timer_track=1,.timer_voice=1};
    alarm_defaults(cfg.alarms);
    birthday_defaults(cfg.birthdays);
    assert(settings_store_save(&cfg)==SETTINGS_STORE_OCCUPIED);
    assert(test_flash[SLOT0_OFFSET]==0x42);
    puts("settings_store_test: PASS");
}
