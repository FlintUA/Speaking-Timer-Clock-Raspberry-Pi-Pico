#include "music_catalog.h"
#include <string.h>
void music_catalog_add(uint8_t bits[MUSIC_CATALOG_BYTES], const uint8_t name[11],
                       uint8_t attributes, uint32_t size, bool cluster_valid) {
    if (!bits || !name || (attributes & 0x18u) || !size || !cluster_valid ||
        name[0] < '0' || name[0] > '9' || name[1] < '0' || name[1] > '9' ||
        name[2] < '0' || name[2] > '9' || memcmp(name+3,"     MP3",8)) return;
    unsigned n=(unsigned)(name[0]-'0')*100u+(unsigned)(name[1]-'0')*10u+(unsigned)(name[2]-'0');
    if (n && n<=255u) bits[n/8u] |= (uint8_t)(1u<<(n%8u));
}
unsigned music_catalog_count(const uint8_t bits[MUSIC_CATALOG_BYTES]) {
    unsigned count=0;
    for(unsigned n=1;n<=255u;++n) if(bits[n/8u]&(1u<<(n%8u))) ++count;
    return count;
}
uint8_t music_catalog_pick(const uint8_t bits[MUSIC_CATALOG_BYTES], uint32_t random,
                           uint8_t previous) {
    unsigned count=music_catalog_count(bits);
    if (!count) return 0;
    bool skip=count>1u && previous && (bits[previous/8u]&(1u<<(previous%8u)));
    unsigned selected=random%(count-(skip?1u:0u));
    for(unsigned n=1;n<=255u;++n) {
        if(!(bits[n/8u]&(1u<<(n%8u))) || (skip && n==previous)) continue;
        if(!selected--) return (uint8_t)n;
    }
    return 0;
}
