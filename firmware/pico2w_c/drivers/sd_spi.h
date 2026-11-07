#pragma once
#include <stdbool.h>
#include <stdint.h>

// Experimental, read-only SD SPI1 bring-up: GP8 RX, GP9 CS, GP10 SCK, GP11 TX.
// All public functions run synchronously from USB commands (not audio callback).
typedef struct {
    bool ready;
    bool high_capacity;
    uint8_t card_type;
    uint32_t first_sector;
    uint32_t sector_count;
    uint8_t fat_type;
} sd_spi_info_t;

bool sd_spi_init_card(void);
bool sd_spi_read_sector(uint32_t lba, uint8_t *sector512);
const sd_spi_info_t *sd_spi_info(void);
bool sd_spi_probe_fat(void);
// Boot/playback preparation: initialize + FAT32 probe on demand.
// Reuses an already ready card; failed attempts are rate-limited (30 s).
// Safe only from the cooperative main loop, never an A2DP callback.
bool sd_spi_ensure_fat32(void);

