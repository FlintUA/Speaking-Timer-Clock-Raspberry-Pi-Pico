#pragma once
#include <stdint.h>
#include <stddef.h>
#define PICO_FLASH_SIZE_BYTES (1024u * 1024u)
#define FLASH_SECTOR_SIZE 4096u
#define FLASH_PAGE_SIZE 256u
void flash_range_erase(uint32_t offset, size_t length);
void flash_range_program(uint32_t offset, const uint8_t *data, size_t length);
