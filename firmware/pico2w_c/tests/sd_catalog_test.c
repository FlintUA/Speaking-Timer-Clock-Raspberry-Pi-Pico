#include <assert.h>
#include <string.h>
#include "../drivers/sd_fat32.c"
static uint8_t disk[8][512];
static bool fail;
static sd_spi_info_t card={.ready=true,.fat_type=32};
const sd_spi_info_t *sd_spi_info(void) {return &card;}
bool sd_spi_ensure_fat32(void) {return card.ready;}
bool sd_spi_read_sector(uint32_t lba,uint8_t *data) {
 if(lba>=8 || (fail && lba==7)) return false;
 memcpy(data,disk[lba],512);return true;
}
static void le32(uint8_t *p,uint32_t n) {p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);p[2]=(uint8_t)(n>>16);p[3]=(uint8_t)(n>>24);}
static void entry(unsigned index,const char *name) {
 uint8_t *p=disk[3+index/16]+(index%16)*32;
 memcpy(p,name,11);p[11]=0x20;p[26]=9;p[28]=1;
}
int main(void) {
 disk[0][11]=0;disk[0][12]=2;disk[0][13]=1;disk[0][14]=1;disk[0][16]=1;
 le32(disk[0]+36,1);le32(disk[0]+44,2);le32(disk[0]+32,100);
 memcpy(disk[2],"08         ",11);disk[2][11]=0x10;disk[2][26]=3;
 for(unsigned c=3;c<=7;++c) {le32(disk[1]+c*4,c==7?0x0fffffff:c+1);for(unsigned i=0;i<16;++i) disk[c][i*32]=0xe5;}
 entry(0,"001     MP3");entry(20,"100     MP3");entry(70,"255     MP3");
 uint8_t bits[32];assert(sd_fat32_music_catalog(bits));
 assert(music_catalog_count(bits)==3 && music_catalog_pick(bits,2,0)==255); // Beyond old 64-entry print limit.
 fail=true;assert(!sd_fat32_music_catalog(bits) && !music_catalog_count(bits));
 fail=false;le32(disk[1]+3*4,3);assert(!sd_fat32_music_catalog(bits) && !music_catalog_count(bits));
 card.ready=false;assert(!sd_fat32_music_catalog(bits) && !music_catalog_count(bits));
 return 0;
}
