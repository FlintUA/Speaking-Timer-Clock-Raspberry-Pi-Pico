#include "music_playlist.h"
#include <string.h>

static uint32_t random_next(music_playlist_t *p) {
    uint32_t x=p->random_state;
    x^=x<<13; x^=x>>17; x^=x<<5;
    return p->random_state=x;
}
static void mix(music_playlist_t *p, unsigned first, unsigned end) {
    for(unsigned i=end;i>first+1;--i) {
        unsigned j=first+random_next(p)%(i-first);
        uint8_t value=p->bag[i-1]; p->bag[i-1]=p->bag[j]; p->bag[j]=value;
    }
}
static bool recent(music_playlist_t *p, uint8_t track) {
    for(unsigned i=0;i<p->recent_size;++i) if(p->recent[i]==track) return true;
    return false;
}
static void remember(music_playlist_t *p) {
    unsigned guard=p->count>7 ? 7u : (unsigned)p->count-1u;
    if(!guard) return;
    if(p->recent_size>=guard) {
        memmove(p->recent,p->recent+1,guard-1); p->recent_size=guard-1;
    }
    p->recent[p->recent_size++]=p->current;
}
static void refill(music_playlist_t *p) {
    unsigned safe=0;
    for(unsigned i=1;i<=p->count;++i) if(!recent(p,(uint8_t)i)) p->bag[safe++]=(uint8_t)i;
    mix(p,0,safe);
    unsigned end=safe;
    for(unsigned i=1;i<=p->count;++i) if(recent(p,(uint8_t)i)) p->bag[end++]=(uint8_t)i;
    mix(p,safe,end);
    p->bag_size=end; p->bag_next=0;
}
static void append_history(music_playlist_t *p) {
    if(p->history_size==MUSIC_HISTORY_MAX) {
        memmove(p->history,p->history+1,MUSIC_HISTORY_MAX-1); --p->history_size;
    }
    p->history[p->history_size++]=p->current;
    p->history_position=p->history_size-1;
    remember(p);
}
void music_playlist_init(music_playlist_t *p, uint8_t count, uint32_t seed) {
    *p=(music_playlist_t){.count=count?count:1,.mode=MUSIC_SHUFFLE,.random_state=seed?seed:1};
    refill(p); p->current=p->bag[p->bag_next++]; append_history(p);
}
void music_playlist_cycle_mode(music_playlist_t *p) {
    p->mode=(music_mode_t)((p->mode+1)%3);
    // A new mode starts a new future while preserving Previous history.
    p->history_size=p->history_position+1;
    if(p->mode==MUSIC_SHUFFLE) { remember(p); refill(p); }
}
uint8_t music_playlist_next(music_playlist_t *p, bool automatic) {
    if(automatic && p->mode==MUSIC_REPEAT_ONE) return p->current;
    if(p->history_position+1<p->history_size) {
        p->current=p->history[++p->history_position]; return p->current;
    }
    if(p->mode==MUSIC_SHUFFLE) {
        if(p->bag_next>=p->bag_size) refill(p);
        p->current=p->bag[p->bag_next++];
    } else p->current=p->current>=p->count ? 1 : (uint8_t)(p->current+1);
    append_history(p);
    return p->current;
}
uint8_t music_playlist_previous(music_playlist_t *p) {
    if(p->history_position) p->current=p->history[--p->history_position];
    else if(p->mode==MUSIC_NORMAL) {
        p->current=p->current<=1 ? p->count : (uint8_t)(p->current-1);
        p->history[0]=p->current;
    }
    return p->current;
}
const char *music_playlist_mode_name(music_mode_t mode) {
    switch(mode) {
        case MUSIC_NORMAL: return "NORM";
        case MUSIC_SHUFFLE: return "SHUF";
        case MUSIC_REPEAT_ONE: return "REP1";
    }
    return "?";
}
