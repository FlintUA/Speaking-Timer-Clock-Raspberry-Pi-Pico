#pragma once
#include <stdbool.h>
#include <stdint.h>
#define MUSIC_CATALOG_BYTES 32u
// Strict numeric 8.3 MP3 IDs, independent of directory order and gaps.
void music_catalog_add(uint8_t bits[MUSIC_CATALOG_BYTES], const uint8_t name[11],
                       uint8_t attributes, uint32_t size, bool cluster_valid);
unsigned music_catalog_count(const uint8_t bits[MUSIC_CATALOG_BYTES]);
uint8_t music_catalog_pick(const uint8_t bits[MUSIC_CATALOG_BYTES], uint32_t random,
                           uint8_t previous);
