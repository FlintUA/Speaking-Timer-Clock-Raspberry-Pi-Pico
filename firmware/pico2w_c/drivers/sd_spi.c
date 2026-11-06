#include "sd_spi.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

#define SD_SPI spi1
#define SD_PIN_MISO 8
#define SD_PIN_CS 9
#define SD_PIN_SCK 10
#define SD_PIN_MOSI 11
#define SD_SECTOR_SIZE 512

static sd_spi_info_t sd_info;
static uint8_t sector[SD_SECTOR_SIZE];

static uint8_t xfer(uint8_t b){
    uint8_t rx = 0xff;
    spi_write_read_blocking(SD_SPI, &b, &rx, 1);
    return rx;
}
static void deselect(void){
    gpio_put(SD_PIN_CS, 1);
    xfer(0xff);
}
static void select_card(void){
    gpio_put(SD_PIN_CS, 0);
    xfer(0xff);
}
static uint8_t wait_ready(unsigned loops){
    for (unsigned n=0;n<loops;n++){
        uint8_t value=xfer(0xff);
        if(value==0xff) return value;
    }
    return 0;
}
static uint8_t command(uint8_t cmd, uint32_t arg, uint8_t crc){
    deselect();
    select_card();
    if (!wait_ready(10000u)) return 0xff;
    xfer((uint8_t)(0x40u | cmd));
    xfer((uint8_t)(arg >> 24));
    xfer((uint8_t)(arg >> 16));
    xfer((uint8_t)(arg >> 8));
    xfer((uint8_t)arg);
    xfer(crc);
    if(cmd==12) xfer(0xff);
    for(unsigned i=0;i<16;i++){
        uint8_t r=xfer(0xff);
        if((r & 0x80u)==0) return r;
    }
    return 0xff;
}
static uint32_t le32(const uint8_t *p){
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
           ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static uint16_t le16(const uint8_t *p){
    return (uint16_t)(p[0] | ((uint16_t)p[1]<<8));
}
const sd_spi_info_t *sd_spi_info(void){return &sd_info;}
bool sd_spi_init_card(void){
    memset(&sd_info,0,sizeof(sd_info));
    spi_init(SD_SPI,400000);
    gpio_set_function(SD_PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(SD_PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(SD_PIN_MOSI, GPIO_FUNC_SPI);
    gpio_init(SD_PIN_CS);
    gpio_set_dir(SD_PIN_CS, GPIO_OUT);
    gpio_put(SD_PIN_CS,1);
    for(int i=0;i<12;i++) xfer(0xff); // >= 74 clock edges in idle
    uint8_t r=command(0,0,0x95);
    deselect();
    if(r!=1){
        printf("[SD] CMD0 failed, R1=0x%02x\n",r);
        return false;
    }
    r=command(8,0x1aau,0x87);
    bool v2=(r==1);
    if(v2){
        uint8_t response[4];
        for(int i=0;i<4;i++) response[i]=xfer(0xff);
        if(response[2]!=0x01 || response[3]!=0xaa){
            puts("[SD] CMD8 voltage/check pattern mismatch");
            deselect();
            return false;
        }
    }else if(r!=0x05){
        printf("[SD] CMD8 unsupported response 0x%02x\n",r);
        deselect();
        return false;
    }
    deselect();
    bool initialized=false;
    for(unsigned i=0;i<1500;i++){
        r=command(55,0,0x01);
        deselect();
        if(r>1) break;
        r=command(41,v2 ? 0x40000000u : 0,0x01);
        deselect();
        if(r==0){initialized=true;break;}
        if(r>1) break;
        sleep_ms(1);
    }
    if(!initialized){
        printf("[SD] ACMD41 initialization timeout/R1=0x%02x\n",r);
        return false;
    }
    r=command(58,0,0x01);
    if(r!=0){
        printf("[SD] CMD58 failed R1=0x%02x\n",r);
        deselect();
        return false;
    }
    uint8_t ocr[4];
    for(int i=0;i<4;i++)ocr[i]=xfer(0xff);
    deselect();
    sd_info.high_capacity = v2 && ((ocr[0]&0x40u)!=0);
    sd_info.card_type = sd_info.high_capacity ? 2 : 1;
    if(!sd_info.high_capacity){
        r=command(16,512,0x01);
        deselect();
        if(r!=0){printf("[SD] CMD16 failed R1=0x%02x\n",r);return false;}
    }
    spi_set_baudrate(SD_SPI,4000000);
    sd_info.ready=true;
    printf("[SD] SPI1 initialized: %s, bus=%lu Hz, read-only\n",
           sd_info.high_capacity ? "SDHC/SDXC" : "SDSC",
           (unsigned long)spi_get_baudrate(SD_SPI));
    return true;
}
bool sd_spi_read_sector(uint32_t lba,uint8_t *dst){
    if(!sd_info.ready || dst==NULL)return false;
    uint32_t addr=sd_info.high_capacity ? lba : lba*512u;
    uint8_t r=command(17,addr,0x01);
    if(r!=0){printf("[SD] CMD17 lba=%lu R1=0x%02x\n",(unsigned long)lba,r);deselect();return false;}
    uint8_t token=0xff;
    // 4 MHz, around 250 ms timeout, leave an upper bound even on absent card.
    for(unsigned i=0;i<125000;i++){
        token=xfer(0xff);
        if(token!=0xff)break;
    }
    if(token!=0xfe){
        printf("[SD] Read token lba=%lu: 0x%02x\n",(unsigned long)lba,token);
        deselect();return false;
    }
    for(int i=0;i<512;i++)dst[i]=xfer(0xff);
    xfer(0xff);xfer(0xff); // CRC16 discarded in first read-only test
    deselect();
    return true;
}
bool sd_spi_probe_fat(void){
    if(!sd_spi_read_sector(0,sector))return false;
    uint32_t boot=0;
    if(sector[510]!=0x55 || sector[511]!=0xaa){
        puts("[SD] No 0x55AA signature in LBA0");return false;
    }
    if(le16(sector+11)!=512 || sector[13]==0){
        uint8_t type=sector[450];
        if(type!=0x0b && type!=0x0c && type!=0x06 && type!=0x0e){
            printf("[SD] Unsupported partition type 0x%02x\n",type);
            return false;
        }
        boot=le32(sector+454);
        if(!sd_spi_read_sector(boot,sector))return false;
    }
    if(sector[510]!=0x55 || sector[511]!=0xaa || le16(sector+11)!=512){
        puts("[SD] Invalid FAT boot sector");return false;
    }
    uint8_t spc=sector[13];
    uint16_t reserved=le16(sector+14);
    uint8_t fats=sector[16];
    uint32_t fat_size=le16(sector+22);
    if(!fat_size)fat_size=le32(sector+36);
    uint32_t total=le16(sector+19);
    if(!total)total=le32(sector+32);
    uint32_t root_sectors=((uint32_t)le16(sector+17)*32u+511u)/512u;
    uint32_t overhead=(uint32_t)reserved+(uint32_t)fats*fat_size+root_sectors;
    if(!spc || (spc&(spc-1u)) || !reserved || !fats || total<=overhead){
        puts("[SD] Invalid FAT geometry");return false;
    }
    uint32_t clusters=(total-overhead)/spc;
    uint8_t fat=clusters<4085u ? 12 : (clusters<65525u ? 16 : 32);
    sd_info.first_sector=boot;
    sd_info.sector_count=total;
    sd_info.fat_type=fat;
    printf("[SD] FAT%u detected at LBA %lu, %lu sectors, sectors/cluster=%u\n",
           (unsigned)fat,(unsigned long)boot,(unsigned long)total,(unsigned)spc);
    return fat==32;
}
