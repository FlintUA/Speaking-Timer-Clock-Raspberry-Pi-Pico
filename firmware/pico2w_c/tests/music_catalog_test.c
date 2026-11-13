#include <assert.h>
#include <string.h>
#include "music_catalog.h"
int main(void) {
 uint8_t bits[32]={0};
 music_catalog_add(bits,(const uint8_t *)"001     MP3",0x20,1,true);
 music_catalog_add(bits,(const uint8_t *)"100     MP3",0x20,1,true);
 music_catalog_add(bits,(const uint8_t *)"255     MP3",0x20,1,true);
 music_catalog_add(bits,(const uint8_t *)"255     MP3",0x20,1,true);
 const char *bad[]={"000     MP3","256     MP3","999     MP3","01      MP3","001X    MP3","002     TXT"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) music_catalog_add(bits,(const uint8_t *)bad[i],0x20,1,true);
 music_catalog_add(bits,(const uint8_t *)"002     MP3",0x10,1,true);
 music_catalog_add(bits,(const uint8_t *)"003     MP3",0x20,0,true);
 music_catalog_add(bits,(const uint8_t *)"004     MP3",0x20,1,false);
 assert(music_catalog_count(bits)==3);
 for(unsigned i=0;i<1000;++i) {uint8_t n=music_catalog_pick(bits,i,100);assert(n==1 || n==255);}
 assert(music_catalog_pick(bits,0,0)==1 && music_catalog_pick(bits,1,0)==100 && music_catalog_pick(bits,2,0)==255);
 memset(bits,0,32);assert(!music_catalog_pick(bits,UINT32_MAX,0));
 music_catalog_add(bits,(const uint8_t *)"255     MP3",0x20,1,true);
 assert(music_catalog_pick(bits,0,255)==255);
 return 0;
}
