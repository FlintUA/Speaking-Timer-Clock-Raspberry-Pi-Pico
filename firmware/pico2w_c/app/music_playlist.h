#pragma once
#include <stdbool.h>
#include <stdint.h>

#define MUSIC_HISTORY_MAX 128u
typedef enum { MUSIC_NORMAL, MUSIC_SHUFFLE, MUSIC_REPEAT_ONE } music_mode_t;
typedef struct {
    uint8_t count, current;
    music_mode_t mode;
    uint8_t bag[255], recent[7], history[MUSIC_HISTORY_MAX];
    unsigned bag_size, bag_next, recent_size, history_size, history_position;
    uint32_t random_state;
} music_playlist_t;

void music_playlist_init(music_playlist_t *p, uint8_t count, uint32_t seed);
void music_playlist_cycle_mode(music_playlist_t *p);
uint8_t music_playlist_next(music_playlist_t *p, bool automatic);
uint8_t music_playlist_previous(music_playlist_t *p);
const char *music_playlist_mode_name(music_mode_t mode);
