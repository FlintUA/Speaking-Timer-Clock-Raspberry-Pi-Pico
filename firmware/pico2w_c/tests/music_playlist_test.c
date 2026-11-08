#include <assert.h>
#include <string.h>
#include "music_playlist.h"

static void shuffle_cycles(unsigned count) {
    music_playlist_t p;
    music_playlist_init(&p,(uint8_t)count,42);
    bool seen[256]={false}; uint8_t tail[7]={0};
    unsigned guard=count>7 ? 7 : count-1;
    for(unsigned cycle=0;cycle<4;++cycle) {
        memset(seen,0,sizeof(seen));
        uint8_t next_tail[7]={0};
        for(unsigned i=0;i<count;++i) {
            uint8_t track=cycle==0 && i==0 ? p.current : music_playlist_next(&p,true);
            assert(track>=1 && track<=count && !seen[track]); seen[track]=true;
            if(cycle && i<count-guard)
                for(unsigned j=0;j<guard;++j) assert(track!=tail[j]);
            if(i>=count-guard) next_tail[i-(count-guard)]=track;
        }
        memcpy(tail,next_tail,sizeof(tail));
    }
}
int main(void) {
    shuffle_cycles(1); shuffle_cycles(2); shuffle_cycles(7);
    shuffle_cycles(8); shuffle_cycles(78); shuffle_cycles(255);
    music_playlist_t p;
    music_playlist_init(&p,3,0);
    assert(p.mode==MUSIC_SHUFFLE);
    uint8_t first=p.current, second=music_playlist_next(&p,false);
    assert(second!=first);
    assert(music_playlist_previous(&p)==first);
    assert(music_playlist_next(&p,false)==second); // Forward after Previous.
    music_playlist_cycle_mode(&p);
    assert(p.mode==MUSIC_REPEAT_ONE);
    for(unsigned i=0;i<5;i++) assert(music_playlist_next(&p,true)==second);
    assert(music_playlist_next(&p,false)!=second); // Explicit Next overrides repeat-one.
    music_playlist_cycle_mode(&p);
    assert(p.mode==MUSIC_NORMAL);
    uint8_t current=p.current;
    assert(music_playlist_next(&p,true)==(current==3?1:current+1));
    for(unsigned i=0;i<300;i++) music_playlist_next(&p,true);
    assert(p.history_size==MUSIC_HISTORY_MAX);
    for(unsigned i=0;i<300;i++) assert(music_playlist_previous(&p)>=1);
    assert(p.history_position==0 && p.current<=3);
    music_playlist_cycle_mode(&p);
    assert(p.mode==MUSIC_SHUFFLE && p.history_size==1);
    music_playlist_init(&p,2,42);
    first=p.current;
    music_playlist_cycle_mode(&p); music_playlist_cycle_mode(&p); music_playlist_cycle_mode(&p);
    assert(music_playlist_next(&p,true)!=first); // Re-enter shuffle avoids the current song.
    music_playlist_init(&p,0,0);
    for(unsigned i=0;i<10;i++) assert(music_playlist_next(&p,true)==1);
    assert(!strcmp(music_playlist_mode_name(MUSIC_REPEAT_ONE),"REP1"));
    return 0;
}
