#include "sd_fat32.h"
#include "sd_spi.h"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SECTOR_BYTES 512u
#define FAT32_END 0x0ffffff8u
#define MAX_DIRECTORY_CLUSTERS 64u
#define MAX_DIRECTORY_ENTRIES 64u

typedef struct {
    uint32_t fat_start;
    uint32_t data_start;
    uint32_t root_cluster;
    uint32_t cluster_count;
    uint8_t sectors_per_cluster;
    bool mounted;
} fat32_volume_t;

typedef struct {
    char name[13];
    uint32_t cluster;
    uint32_t size;
    bool directory;
} fat32_entry_t;

static fat32_volume_t volume;
static uint8_t sector[SECTOR_BYTES];

static uint16_t read_le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static bool valid_cluster(uint32_t cluster) {
    return cluster >= 2u && cluster < volume.cluster_count + 2u;
}
static bool cluster_to_lba(uint32_t cluster, uint32_t *lba) {
    if (!valid_cluster(cluster)) return false;
    *lba = volume.data_start +
           (cluster - 2u) * (uint32_t)volume.sectors_per_cluster;
    return true;
}
static bool next_cluster(uint32_t current, uint32_t *next) {
    if (!valid_cluster(current)) return false;
    uint32_t offset = current * 4u;
    if (!sd_spi_read_sector(volume.fat_start + offset / SECTOR_BYTES, sector))
        return false;
    *next = read_le32(sector + (offset % SECTOR_BYTES)) & 0x0fffffffu;
    return true;
}
static bool mount_volume(void) {
    const sd_spi_info_t *card = sd_spi_info();
    memset(&volume, 0, sizeof(volume));
    if (!card->ready || card->fat_type != 32u) {
        puts("[SD-FAT] Run sd-test first; FAT32 required");
        return false;
    }
    if (!sd_spi_read_sector(card->first_sector, sector)) {
        puts("[SD-FAT] Cannot read FAT32 boot sector");
        return false;
    }
    uint16_t bytes_per_sector = read_le16(sector + 11);
    uint8_t spc = sector[13];
    uint16_t reserved = read_le16(sector + 14);
    uint8_t fat_count = sector[16];
    uint32_t sectors_per_fat = read_le32(sector + 36);
    uint32_t root_cluster = read_le32(sector + 44);
    uint32_t total_sectors = read_le32(sector + 32);
    uint32_t overhead = (uint32_t)reserved +
                        (uint32_t)fat_count * sectors_per_fat;
    if (bytes_per_sector != 512u || !spc || spc > 128u ||
        (spc & (spc - 1u)) || !reserved || !fat_count ||
        !sectors_per_fat || overhead >= total_sectors) {
        puts("[SD-FAT] Unsupported FAT32 boot parameters");
        return false;
    }
    volume.fat_start = card->first_sector + reserved;
    volume.data_start = card->first_sector + overhead;
    volume.root_cluster = root_cluster;
    volume.cluster_count = (total_sectors - overhead) / spc;
    volume.sectors_per_cluster = spc;
    if (!valid_cluster(root_cluster)) {
        puts("[SD-FAT] Invalid FAT32 root cluster");
        return false;
    }
    volume.mounted = true;
    return true;
}
static bool make_short_name(const char *name, uint8_t out[11]) {
    if (!name || !name[0]) return false;
    memset(out, ' ', 11);
    const char *dot = strchr(name, '.');
    size_t base_len = dot ? (size_t)(dot - name) : strlen(name);
    size_t ext_len = dot ? strlen(dot + 1) : 0u;
    if (!base_len || base_len > 8u || ext_len > 3u ||
        (dot && strchr(dot + 1, '.')) || strchr(name, '/') ||
        strchr(name, '\\')) return false;
    for (size_t i = 0; i < base_len; ++i) {
        unsigned char ch = (unsigned char)name[i];
        if (ch <= ' ' || ch >= 0x7fu || ch == '.') return false;
        out[i] = (uint8_t)toupper(ch);
    }
    for (size_t i = 0; i < ext_len; ++i) {
        unsigned char ch = (unsigned char)dot[i + 1u];
        if (ch <= ' ' || ch >= 0x7fu) return false;
        out[8u + i] = (uint8_t)toupper(ch);
    }
    return true;
}
static void parse_entry(const uint8_t *raw, fat32_entry_t *entry) {
    size_t p = 0;
    for (unsigned i = 0; i < 8u && raw[i] != ' '; ++i)
        entry->name[p++] = (char)raw[i];
    if (raw[8] != ' ') {
        entry->name[p++] = '.';
        for (unsigned i = 8u; i < 11u && raw[i] != ' '; ++i)
            entry->name[p++] = (char)raw[i];
    }
    entry->name[p] = '\0';
    entry->directory = (raw[11] & 0x10u) != 0;
    entry->cluster = ((uint32_t)read_le16(raw + 20) << 16) |
                     read_le16(raw + 26);
    entry->size = read_le32(raw + 28);
}
static bool walk_directory(uint32_t first_cluster, const uint8_t *search,
                           fat32_entry_t *found, bool list_mode) {
    uint32_t cluster = first_cluster;
    unsigned shown = 0;
    for (unsigned chain = 0; chain < MAX_DIRECTORY_CLUSTERS; ++chain) {
        uint32_t first_lba;
        if (!cluster_to_lba(cluster, &first_lba)) {
            puts("[SD-FAT] Invalid directory cluster");
            return false;
        }
        for (unsigned sec = 0; sec < volume.sectors_per_cluster; ++sec) {
            if (!sd_spi_read_sector(first_lba + sec, sector)) {
                puts("[SD-FAT] Directory read error");
                return false;
            }
            for (unsigned pos = 0; pos < 512u; pos += 32u) {
                const uint8_t *raw = &sector[pos];
                if (raw[0] == 0x00) {
                    if (list_mode) printf("[SD-LIST] End of directory (%u entries shown)\n", shown);
                    return list_mode;
                }
                if (raw[0] == 0xe5 || raw[11] == 0x0f ||
                    (raw[11] & 0x08u)) continue; // deleted/LFN/volume label
                if (!list_mode && search && memcmp(raw, search, 11u) == 0) {
                    if (found) parse_entry(raw, found);
                    return true;
                }
                if (list_mode) {
                    fat32_entry_t entry;
                    parse_entry(raw, &entry);
                    printf("[SD-LIST] %-12s %s %lu bytes\n",
                           entry.name, entry.directory ? "DIR " : "FILE",
                           (unsigned long)entry.size);
                    if (++shown >= MAX_DIRECTORY_ENTRIES) {
                        puts("[SD-LIST] Output limit reached (64 entries)");
                        return true;
                    }
                }
            }
        }
        uint32_t next;
        if (!next_cluster(cluster, &next)) {
            puts("[SD-FAT] FAT chain read error");
            return false;
        }
        if (next >= FAT32_END) {
            if (list_mode) printf("[SD-LIST] End of FAT chain (%u entries shown)\n", shown);
            return list_mode;
        }
        if (!valid_cluster(next) || next == cluster) {
            puts("[SD-FAT] Corrupt directory FAT chain");
            return false;
        }
        cluster = next;
    }
    puts("[SD-FAT] Directory scan limit (64 clusters) exceeded");
    return false;
}
static bool resolve_directory(const char *dir, uint32_t *out) {
    if (!dir || !dir[0] || (dir[0] == '/' && !dir[1])) {
        *out = volume.root_cluster;
        return true;
    }
    if (*dir == '/') ++dir;
    uint8_t key[11];
    if (!make_short_name(dir, key)) {
        puts("[SD-FAT] Directory name must be 8.3 (e.g. 07)");
        return false;
    }
    fat32_entry_t entry;
    if (!walk_directory(volume.root_cluster, key, &entry, false) ||
        !entry.directory || !valid_cluster(entry.cluster)) {
        printf("[SD-FAT] Directory not found: %s\n", dir);
        return false;
    }
    *out = entry.cluster;
    return true;
}
bool sd_fat32_list(const char *directory) {
    if (!mount_volume()) return false;
    uint32_t dir_cluster;
    if (!resolve_directory(directory, &dir_cluster)) return false;
    printf("[SD-LIST] Listing /%s\n", directory && directory[0] ? directory : "");
    return walk_directory(dir_cluster, NULL, NULL, true);
}
bool sd_fat32_file(const char *path) {
    if (!mount_volume()) return false;
    if (!path) return false;
    if (*path == '/') ++path;
    const char *slash = strchr(path, '/');
    char dir[13] = {0};
    const char *file_name = path;
    if (slash) {
        size_t len = (size_t)(slash - path);
        if (len == 0 || len >= sizeof(dir)) {
            puts("[SD-FILE] Invalid directory component"); return false;
        }
        memcpy(dir, path, len);
        file_name = slash + 1;
    }
    uint8_t key[11];
    if (!make_short_name(file_name, key)) {
        puts("[SD-FILE] Filename must be 8.3 (e.g. 07/001.mp3)");
        return false;
    }
    uint32_t dir_cluster;
    if (!resolve_directory(dir, &dir_cluster)) return false;
    fat32_entry_t entry;
    if (!walk_directory(dir_cluster, key, &entry, false) || entry.directory) {
        printf("[SD-FILE] Not found: %s\n", path);
        return false;
    }
    printf("[SD-FILE] Found /%s: %lu bytes, first cluster=%lu\n",
           path, (unsigned long)entry.size, (unsigned long)entry.cluster);
    if (!entry.size) {
        puts("[SD-FILE] Empty file");
        return true;
    }
    uint32_t first_lba;
    if (!cluster_to_lba(entry.cluster, &first_lba) ||
        !sd_spi_read_sector(first_lba, sector)) {
        puts("[SD-FILE] Failed to read first data sector");
        return false;
    }
    unsigned count = entry.size < 16u ? (unsigned)entry.size : 16u;
    printf("[SD-FILE] First %u bytes:", count);
    for (unsigned i=0;i<count;++i) printf(" %02X", sector[i]);
    putchar('\n');
    puts("[SD-FILE] PASS: file lookup and first data sector read (no decoding)");
    return true;
}


// Diagnostic full-file reader: one 512-byte sector per main-loop iteration.
// CRC32 verifies sequential logical file bytes, including non-contiguous FAT32
// cluster chains. It never writes sectors or changes clock settings.
#define STREAM_MAX_BYTES (2u * 1024u * 1024u)
typedef struct {
    bool active;
    uint32_t total;
    uint32_t consumed;
    uint32_t cluster;
    uint32_t sector_index;
    uint32_t chain_hops;
    uint32_t crc;
} fat32_stream_t;
static fat32_stream_t stream;

bool sd_fat32_stream_active(void) {
    return stream.active;
}
void sd_fat32_stream_stop(void) {
    if (stream.active) puts("[SD-STREAM] Cancelled by user");
    stream.active = false;
}
bool sd_fat32_stream_start(const char *path) {
    sd_fat32_stream_stop();
    if (!mount_volume() || !path) return false;
    if (*path == '/') ++path;
    const char *slash = strchr(path, '/');
    char directory[13] = {0};
    const char *file = path;
    if (slash) {
        size_t len = (size_t)(slash - path);
        if (len == 0 || len >= sizeof(directory)) {
            puts("[SD-STREAM] Invalid folder component");
            return false;
        }
        memcpy(directory, path, len);
        file = slash + 1;
    }
    uint8_t name[11];
    if (!make_short_name(file, name)) {
        puts("[SD-STREAM] Use DOS 8.3 filename, e.g. 07/001.mp3");
        return false;
    }
    uint32_t dir_cluster;
    if (!resolve_directory(directory, &dir_cluster)) return false;
    fat32_entry_t entry;
    if (!walk_directory(dir_cluster, name, &entry, false) ||
        entry.directory || !valid_cluster(entry.cluster)) {
        puts("[SD-STREAM] File missing or invalid first cluster");
        return false;
    }
    if (entry.size == 0 || entry.size > STREAM_MAX_BYTES) {
        printf("[SD-STREAM] File size %lu unsupported (1..2097152 bytes)\n",
               (unsigned long)entry.size);
        return false;
    }
    memset(&stream, 0, sizeof(stream));
    stream.active = true;
    stream.total = entry.size;
    stream.cluster = entry.cluster;
    stream.crc = 0xffffffffu;
    printf("[SD-STREAM] START /%s size=%lu first_cluster=%lu (read-only, one sector/loop)\n",
           path, (unsigned long)stream.total, (unsigned long)stream.cluster);
    return true;
}
void sd_fat32_stream_service(void) {
    if (!stream.active) return;
    if (!valid_cluster(stream.cluster)) {
        puts("[SD-STREAM] FAIL: invalid data cluster");
        stream.active = false;
        return;
    }
    uint32_t cluster_lba;
    if (!cluster_to_lba(stream.cluster, &cluster_lba) ||
        !sd_spi_read_sector(cluster_lba + stream.sector_index, sector)) {
        puts("[SD-STREAM] FAIL: sector read error");
        stream.active = false;
        return;
    }
    uint32_t remaining = stream.total - stream.consumed;
    uint32_t count = remaining < SECTOR_BYTES ? remaining : SECTOR_BYTES;
    for (uint32_t i = 0; i < count; ++i) {
        stream.crc ^= sector[i];
        for (unsigned bit = 0; bit < 8u; ++bit)
            stream.crc = (stream.crc >> 1) ^
                         ((stream.crc & 1u) ? 0xedb88320u : 0u);
    }
    stream.consumed += count;
    if (stream.consumed == stream.total) {
        printf("[SD-STREAM] PASS bytes=%lu clusters=%lu crc32=%08lX\n",
               (unsigned long)stream.consumed,
               (unsigned long)(stream.chain_hops + 1u),
               (unsigned long)(stream.crc ^ 0xffffffffu));
        stream.active = false;
        return;
    }
    if (++stream.sector_index == volume.sectors_per_cluster) {
        uint32_t next;
        if (!next_cluster(stream.cluster, &next) ||
            !valid_cluster(next) || next == stream.cluster ||
            ++stream.chain_hops > (stream.total / SECTOR_BYTES + 1u)) {
            puts("[SD-STREAM] FAIL: premature end or corrupt FAT cluster chain");
            stream.active = false;
            return;
        }
        stream.cluster = next;
        stream.sector_index = 0;
    }
}


// A separate seekable, read-only cursor for software MP3 decoding.
// Unlike sd-stream CRC32 diagnostics, this supports arbitrary offsets.
// Only one open file is supported and callers must not run concurrently
// with other SD diagnostics. Single shared 512-byte sector scratch buffer.
typedef struct {
    bool open;
    uint32_t first_cluster;
    uint32_t cluster;
    uint32_t size;
    uint32_t position;
    uint32_t cache_lba;
    bool cached;
} sd_file_cursor_t;
static sd_file_cursor_t file_cursor;

void sd_fat32_close_read(void) {
    memset(&file_cursor, 0, sizeof(file_cursor));
}
uint32_t sd_fat32_size(void) {
    return file_cursor.open ? file_cursor.size : 0u;
}
bool sd_fat32_open_read(const char *path) {
    sd_fat32_close_read();
    if (sd_fat32_stream_active()) {
        puts("[SD-MP3] Cannot open during sd-stream CRC test");
        return false;
    }
    // Retry missing or not-yet-probed media only on a real playback request.
    // No card reinitialization is attempted while a file/CRC stream is active.
    // This remains in the main-loop decoder path, never in A2DP callbacks.
    if (!sd_spi_ensure_fat32()) {
        puts("[SD-MP3] FAT32 unavailable; caller may fall back to LOCAL");
        return false;
    }
    if (!mount_volume() || !path) return false;
    if (*path == '/') ++path;
    const char *slash = strchr(path, '/');
    char directory[13] = {0};
    const char *file = path;
    if (slash) {
        size_t len = (size_t)(slash - path);
        if (!len || len >= sizeof(directory)) return false;
        memcpy(directory, path, len);
        file = slash + 1;
    }
    uint8_t short_name[11];
    if (!make_short_name(file, short_name)) return false;
    uint32_t parent_cluster;
    if (!resolve_directory(directory, &parent_cluster)) return false;
    fat32_entry_t entry;
    if (!walk_directory(parent_cluster, short_name, &entry, false) ||
        entry.directory || !entry.size || !valid_cluster(entry.cluster)) return false;
    file_cursor.first_cluster = entry.cluster;
    file_cursor.cluster = entry.cluster;
    file_cursor.size = entry.size;
    file_cursor.open = true;
    return true;
}
bool sd_fat32_seek(uint32_t offset) {
    if (!file_cursor.open || offset > file_cursor.size) return false;
    // Seek-to-end is legal and never requests a data cluster.
    uint32_t bytes_per_cluster = volume.sectors_per_cluster * SECTOR_BYTES;
    uint32_t hops = offset == file_cursor.size && offset ?
        (offset - 1u) / bytes_per_cluster : offset / bytes_per_cluster;
    if (hops >= volume.cluster_count) return false;
    uint32_t cluster = file_cursor.first_cluster;
    for (uint32_t i = 0; i < hops; ++i) {
        uint32_t next;
        if (!next_cluster(cluster, &next) || !valid_cluster(next) ||
            next == cluster) return false;
        cluster = next;
    }
    file_cursor.cluster = cluster;
    file_cursor.position = offset;
    file_cursor.cached = false;
    return true;
}
size_t sd_fat32_read(void *destination, size_t count) {
    if (!file_cursor.open || !destination || !count) return 0;
    uint8_t *out = (uint8_t *)destination;
    size_t read_total = 0;
    uint32_t bytes_per_cluster = volume.sectors_per_cluster * SECTOR_BYTES;
    while (read_total < count && file_cursor.position < file_cursor.size) {
        uint32_t cluster_pos = file_cursor.position % bytes_per_cluster;
        uint32_t sector_index = cluster_pos / SECTOR_BYTES;
        uint32_t offset = cluster_pos % SECTOR_BYTES;
        uint32_t first_lba;
        if (!cluster_to_lba(file_cursor.cluster, &first_lba)) break;
        uint32_t lba = first_lba + sector_index;
        if (!file_cursor.cached || file_cursor.cache_lba != lba) {
            if (!sd_spi_read_sector(lba, sector)) break;
            file_cursor.cached = true;
            file_cursor.cache_lba = lba;
        }
        size_t available = SECTOR_BYTES - offset;
        uint32_t left = file_cursor.size - file_cursor.position;
        if (available > left) available = left;
        if (available > count - read_total) available = count - read_total;
        memcpy(out + read_total, sector + offset, available);
        read_total += available;
        file_cursor.position += (uint32_t)available;
        if (file_cursor.position < file_cursor.size &&
            file_cursor.position % bytes_per_cluster == 0u) {
            uint32_t next;
            file_cursor.cached = false;
            if (!next_cluster(file_cursor.cluster, &next) ||
                !valid_cluster(next) || next == file_cursor.cluster) break;
            file_cursor.cluster = next;
        }
    }
    return read_total;
}
