#include <assert.h>
#include "queue.h"
#include "dfplayer.h"

static bool ready=true, busy;
static unsigned plays, pauses, resumes, volumes;
bool dfplayer_command_ready(uint64_t now) { (void)now; return ready; }
bool dfplayer_busy(void) { return busy; }
void dfplayer_play_folder_track(uint8_t folder,uint8_t track,uint64_t now) {
    (void)now; assert(folder==8 && track>=1 && track<=2); ++plays;
}
void dfplayer_pause(uint64_t now) { (void)now; ++pauses; }
void dfplayer_resume(uint64_t now) { (void)now; ++resumes; }
void dfplayer_set_volume(uint8_t level,uint64_t now) {
    (void)now; assert(level==12); ++volumes;
}

int main(void) {
    audio_queue_t q;
    audio_queue_init(&q);
    assert(audio_queue_idle(&q) && !audio_queue_pause(&q));
    assert(!audio_queue_resume(&q));
    assert(audio_queue_enqueue(&q,8,1));
    // Pausing a not-yet-sent selection must not resume an old module track.
    assert(audio_queue_pause(&q));
    audio_queue_service(&q,0);
    assert(!plays && !pauses && !audio_queue_idle(&q));
    assert(audio_queue_resume(&q));
    audio_queue_service(&q,1);
    assert(plays==1 && !resumes);
    busy=true;
    audio_queue_service(&q,2);
    assert(q.seen_busy);
    assert(audio_queue_enqueue(&q,8,2));
    assert(audio_queue_pause(&q));
    ready=false;
    audio_queue_service(&q,3);
    assert(!pauses && q.pause_pending);
    // Rapid resume cannot overtake its pause on the paced UART.
    assert(audio_queue_resume(&q));
    assert(!audio_queue_resume(&q));
    ready=true;
    audio_queue_service(&q,120);
    assert(pauses==1 && !resumes && q.paused);
    // A second pause cancels an unsent resume.
    assert(audio_queue_pause(&q));
    audio_queue_service(&q,240);
    assert(pauses==2 && !resumes);
    busy=false;
    audio_queue_set_volume(&q,12);
    audio_queue_service(&q,360);
    audio_queue_service(&q,100000);
    assert(volumes==1 && q.track_started && q.size==1 && plays==1);
    assert(!audio_queue_idle(&q));
    assert(audio_queue_resume(&q));
    ready=false;
    audio_queue_service(&q,100001);
    assert(q.paused && !resumes);
    ready=true;
    audio_queue_service(&q,100120);
    assert(resumes==1 && !q.paused && !q.seen_busy && plays==1);
    audio_queue_service(&q,100121);
    assert(plays==1); // BUSY may assert later after resume.
    busy=true;
    audio_queue_service(&q,100300);
    busy=false;
    audio_queue_service(&q,100500);
    assert(plays==2 && q.track_started && !q.size);
    assert(audio_queue_pause(&q));
    assert(audio_queue_resume(&q));
    audio_queue_clear(&q,true); // Exit, mute or alarm discards position.
    assert(!q.paused && !q.resume_pending && !q.track_started);
    assert(!audio_queue_resume(&q));
    audio_queue_service(&q,101000);
    assert(resumes==1 && audio_queue_idle(&q));
    return 0;
}
