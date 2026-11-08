#include <assert.h>
#include "speech.h"
#include "dfplayer.h"

bool dfplayer_busy(void) { return false; }
bool dfplayer_command_ready(uint64_t now) { (void)now; return true; }
void dfplayer_pause(uint64_t now) { (void)now; }
void dfplayer_resume(uint64_t now) { (void)now; }
void dfplayer_set_volume(uint8_t v,uint64_t now) { (void)v; (void)now; }
void dfplayer_play_folder_track(uint8_t f,uint8_t t,uint64_t now) {
    (void)f; (void)t; (void)now;
}

int main(void) {
    ds1302_datetime_t date={.year=2026,.month=11,.day=8,.weekday=1};
    for (unsigned lang=0;lang<2;++lang) {
        audio_queue_t q={0};
        unsigned offset=lang?10:0;
        assert(speech_enqueue_date(&q,(speech_language_t)lang,&date));
        assert(q.size==4);
        assert(q.items[0].folder==6+offset && q.items[0].track==7); // Actual Sunday, not stale RTC weekday.
        assert(q.items[1].folder==3+offset && q.items[1].track==8);
        assert(q.items[2].folder==4+offset && q.items[2].track==11);
        assert(q.items[3].folder==5+offset && q.items[3].track==6);
        assert(speech_enqueue_time(&q,(speech_language_t)lang,8,33,false));
        assert(q.size==6 && q.items[4].folder==1+offset && q.items[4].track==8);
        assert(q.items[5].folder==2+offset && q.items[5].track==33);
    }
    audio_queue_t q={0};
    date.year=2030;
    assert(speech_enqueue_date(&q,SPEECH_RU,&date) && q.size==3);
    date.year=2022;
    audio_queue_init(&q);
    assert(speech_enqueue_date(&q,SPEECH_DE,&date) && q.size==3);
    date.year=2024; date.month=2; date.day=29;
    audio_queue_init(&q);
    assert(speech_enqueue_date(&q,SPEECH_DE,&date));
    date.year=2023;
    unsigned size=q.size;
    assert(!speech_enqueue_date(&q,SPEECH_RU,&date) && q.size==size);
    assert(!speech_enqueue_date(&q,SPEECH_RU,0));
    assert(!speech_enqueue_date(0,SPEECH_RU,&date));
    date.year=2026; date.month=11; date.day=8;
    audio_queue_init(&q);
    for(unsigned i=0;i<13;++i) assert(audio_queue_enqueue(&q,1,1));
    assert(!speech_enqueue_date(&q,SPEECH_RU,&date) && q.size==13);
    // Planner appends correctly through a ring wrap without partial additions.
    audio_queue_init(&q);
    q.head=15;
    assert(speech_enqueue_date(&q,SPEECH_RU,&date));
    assert(q.items[15].track==7 && q.items[0].track==8 && q.items[2].track==6);
    return 0;
}
