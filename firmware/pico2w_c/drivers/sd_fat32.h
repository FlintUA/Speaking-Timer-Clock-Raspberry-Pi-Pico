#pragma once
#include <stdbool.h>

// Read-only FAT32 diagnostics on top of SPI1 sector reads.
// Call sd-test first. These bounded, synchronous diagnostics are not suitable
// for invoking inside an A2DP audio callback or an active streaming pipeline.
bool sd_fat32_list(const char *directory);
bool sd_fat32_file(const char *path);

// Incremental, read-only full-file CRC32: sd-stream <8.3-path>, sd-stop.
bool sd_fat32_stream_start(const char *path);
void sd_fat32_stream_service(void); // Call once per main loop, reads max 512 bytes.
void sd_fat32_stream_stop(void);
bool sd_fat32_stream_active(void);

#include <stddef.h>
#include <stdint.h>

// Separate seekable file cursor for optional MP3 software decoder.
// Not reentrant; use only in diagnostics, never in a BTstack callback.
// Reader is read-only and supports short DOS 8.3 names.
bool sd_fat32_open_read(const char *path);
void sd_fat32_close_read(void);
size_t sd_fat32_read(void *dst, size_t count);
bool sd_fat32_seek(uint32_t offset);
uint32_t sd_fat32_size(void);

// Idle-only full directory scan; no decoder/file cursor may be active.
// Numeric IDs 001..255 in /08, excludes empty/deleted/non-MP3/directory entries.
bool sd_fat32_music_catalog(uint8_t tracks[32]);
