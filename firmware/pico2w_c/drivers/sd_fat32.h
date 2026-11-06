#pragma once
#include <stdbool.h>

// Read-only FAT32 diagnostics on top of SPI1 sector reads.
// Call sd-test first. These bounded, synchronous diagnostics are not suitable
// for invoking inside an A2DP audio callback or an active streaming pipeline.
bool sd_fat32_list(const char *directory);
bool sd_fat32_file(const char *path);
