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
