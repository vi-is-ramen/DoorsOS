#include "fat32.h"
#include "ata.h"
#include "thirdpartycfg.h"
#include "../libs/string.h"
#include "../gfx/printf.h"
#include "ahci.h"
#include "pci.h"
static fat32_info_t fs;
static uint8_t sector[512];
static bool use_ahci = false; 

// --- Utils ---

static uint32_t get_cluster_lba(uint32_t cluster) {
    return fs.start_lba + fs.reserved_sector_count +
           (fs.num_fats * fs.fat_size_sectors) +
           ((cluster - 2) * fs.sectors_per_cluster);
}

static void read_sector(uint32_t lba) {
        if(use_ahci) {
            ahci_read(&host->ports[0], (uint32_t)(lba & 0xFFFFFFFF), (uint32_t)(lba >> 32), 1, sector);

        }
        else{
        ata_pio_read28(lba, 1, sector);
        }
    
}
static void write_sector(uint32_t lba, const uint8_t* data) {
        if(use_ahci) {
            ahci_write(&host->ports[0], (uint32_t)(lba & 0xFFFFFFFF), (uint32_t)(lba >> 32), 1, sector);
        }
        else {ata_pio_write48(lba, 1, (uint8_t*)data);}
    
}



static uint32_t get_fat_sector(uint32_t cluster) {
    return fs.start_lba + fs.reserved_sector_count + ((cluster * 4) / fs.bytes_per_sector);
}

static uint32_t get_fat_entry(uint32_t cluster) {
    uint32_t fat_sector = get_fat_sector(cluster);
    read_sector(fat_sector);
    uint32_t offset = (cluster * 4) % 512;
    return (*(uint32_t*)&sector[offset]) & 0x0FFFFFFF;
}

static void set_fat_entry(uint32_t cluster, uint32_t value) {
    uint32_t fat_sector = get_fat_sector(cluster);
    read_sector(fat_sector);
    uint32_t offset = (cluster * 4) % 512;
    *(uint32_t*)&sector[offset] = value & 0x0FFFFFFF;
    write_sector(fat_sector, sector);
}

static bool is_end_of_cluster_chain(uint32_t cluster) {
    return cluster >= 0x0FFFFFF8;
}

static uint32_t alloc_cluster_chain(uint32_t needed_clusters) {
    uint32_t start = 2;
    uint32_t head = 0, prev = 0, count = 0;

    while (count < needed_clusters && start < 0x0FFFFFF0) {
        uint32_t entry = get_fat_entry(start);
        if (entry == 0x00000000) {
            if (head == 0) head = start;
            if (prev != 0) set_fat_entry(prev, start);
            prev = start;
            count++;
        }
        start++;
    }

    if (count == needed_clusters) {
        set_fat_entry(prev, 0x0FFFFFFF);
        return head;
    }
    return 0;
}

static void clear_cluster(uint32_t cluster) {
    uint32_t lba = get_cluster_lba(cluster);
    uint8_t zero[512] = {0};
    for (uint8_t i = 0; i < fs.sectors_per_cluster; i++)
        write_sector(lba + i, zero);
}

// --- LFN parsing ---

static void parse_lfn_entry(uint8_t* entry, char* lfn_buf, int ord) {
    int offset = (ord - 1) * 13;
    for (int i = 0; i < 5; i++) {
        uint16_t wc = *(uint16_t*)(entry + 1 + i*2);
        if (wc == 0x0000 || wc == 0xFFFF) break;
        lfn_buf[offset++] = (char)(wc & 0xFF);
    }
    for (int i = 0; i < 6; i++) {
        uint16_t wc = *(uint16_t*)(entry + 14 + i*2);
        if (wc == 0x0000 || wc == 0xFFFF) break;
        lfn_buf[offset++] = (char)(wc & 0xFF);
    }
    for (int i = 0; i < 2; i++) {
        uint16_t wc = *(uint16_t*)(entry + 28 + i*2);
        if (wc == 0x0000 || wc == 0xFFFF) break;
        lfn_buf[offset++] = (char)(wc & 0xFF);
    }
}

static void parse_dir_entry(uint8_t* entry, fat32_dir_entry_t* out, const char* lfn_name) {
    if (lfn_name && *lfn_name) {
        size_t len = strlen(lfn_name);
        if (len >= FAT32_MAX_LFN) len = FAT32_MAX_LFN - 1;
        memcpy(out->name, lfn_name, len);
        out->name[len] = '\0';
    } else {
        char rawname[12];
        memcpy(rawname, entry, 11);
        rawname[11] = '\0';

        int name_len = 8;
        while (name_len > 0 && rawname[name_len - 1] == ' ') name_len--;
        int ext_len = 3;
        while (ext_len > 0 && rawname[8 + ext_len - 1] == ' ') ext_len--;

        int pos = 0;
        for (int i = 0; i < name_len && pos < FAT32_MAX_LFN - 1; i++)
            out->name[pos++] = k_toupper(rawname[i]);

        if (ext_len > 0 && pos < FAT32_MAX_LFN - 2) {
            out->name[pos++] = '.';
            for (int i = 0; i < ext_len && pos < FAT32_MAX_LFN - 1; i++)
                out->name[pos++] = k_toupper(rawname[8 + i]);
        }
        out->name[pos] = '\0';
    }

    out->attr = entry[11];
    uint16_t high = *(uint16_t*)&entry[20];
    uint16_t low = *(uint16_t*)&entry[26];
    out->first_cluster = ((uint32_t)high << 16) | low;
    out->size = *(uint32_t*)&entry[28];
    out->is_dir = (entry[11] & 0x10) != 0;
}

// --- Directory traversal ---

static bool find_entry_in_cluster(uint32_t cluster, const char* name, fat32_dir_entry_t* out) {
    char lfn_buf[FAT32_MAX_LFN];
    memset(lfn_buf, 0, sizeof(lfn_buf));

    while (!is_end_of_cluster_chain(cluster)) {
        uint32_t lba = get_cluster_lba(cluster);

        for (uint8_t sec = 0; sec < fs.sectors_per_cluster; sec++) {
            read_sector(lba + sec);

            for (int i = 0; i < 512; i += 32) {
                uint8_t* entry = &sector[i];

                if (entry[0] == 0x00) return false;
                if (entry[0] == 0xE5) continue;
                if ((entry[11] & 0x0F) == 0x0F) {
                    int ord = entry[0] & 0x1F;
                    parse_lfn_entry(entry, lfn_buf, ord);
                    continue;
                }

                fat32_dir_entry_t current_entry;
                parse_dir_entry(entry, &current_entry, lfn_buf);
                memset(lfn_buf, 0, sizeof(lfn_buf));

                if ((current_entry.attr & 0x08) && !(current_entry.attr & 0x10)) continue; // skip volume label

                if (stricmp(current_entry.name, name) == 0) {
                    if (out) *out = current_entry;
                    return true;
                }
            }
        }

        cluster = get_fat_entry(cluster);
    }

    return false;
}

static const char* split_path(const char* path, char* first_component, size_t max_len) {
    size_t i = 0;
    while (path[i] != '\0' && path[i] != '/' && path[i] != '\\') {
        if (i + 1 < max_len)
            first_component[i] = path[i];
        i++;
    }
    first_component[i < max_len ? i : max_len - 1] = '\0';
    if (path[i] == '/' || path[i] == '\\') return path + i + 1;
    return NULL;
}

static bool fat32_find_entry(const char* path, fat32_dir_entry_t* out) {
    char component[FAT32_MAX_LFN];
    uint32_t current_cluster = fs.root_cluster;
    const char* rest = path;

    while (rest) {
        rest = split_path(rest, component, sizeof(component));
        fat32_dir_entry_t entry;

        if (!find_entry_in_cluster(current_cluster, component, &entry))
            return false;

        if (rest) {
            if (!entry.is_dir) return false;
            current_cluster = entry.first_cluster;
        } else {
            if (out) *out = entry;
            return true;
        }
    }

    return find_entry_in_cluster(current_cluster, path, out);
}

// --- Public API ---

bool fat32_mount(uint32_t lba,bool ahci) {
    if (ahci) {
        use_ahci = true;
        ahci_read(&host->ports[0], (uint32_t)(lba & 0xFFFFFFFF), (uint32_t)(lba >> 32), 1, sector);
    }
    else {
    ata_pio_read28(lba, 1, sector);
    }
    

    fs.start_lba = lba;
    fs.bytes_per_sector = *(uint16_t*)&sector[11];
    fs.sectors_per_cluster = sector[13];
    fs.reserved_sector_count = *(uint16_t*)&sector[14];
    fs.num_fats = sector[16];
    fs.fat_size_sectors = *(uint32_t*)&sector[36];
    fs.root_cluster = *(uint32_t*)&sector[44];

    if (fs.bytes_per_sector != 512) {
        printf("Unsupported sector size %u\n", fs.bytes_per_sector);
        return false;
    }

    return true;
}

void fat32_list_root(void) {
    uint32_t cluster = fs.root_cluster;
    char lfn_buf[FAT32_MAX_LFN];
    memset(lfn_buf, 0, sizeof(lfn_buf));

    while (!is_end_of_cluster_chain(cluster)) {
        uint32_t lba = get_cluster_lba(cluster);

        for (uint8_t sec = 0; sec < fs.sectors_per_cluster; sec++) {
            read_sector(lba + sec);

            for (int i = 0; i < 512; i += 32) {
                uint8_t* entry = &sector[i];

                if (entry[0] == 0x00) return;
                if (entry[0] == 0xE5) continue;
                if ((entry[11] & 0x0F) == 0x0F) {
                    int ord = entry[0] & 0x1F;
                    parse_lfn_entry(entry, lfn_buf, ord);
                    continue;
                }

                fat32_dir_entry_t file;
                parse_dir_entry(entry, &file, lfn_buf);
                memset(lfn_buf, 0, sizeof(lfn_buf));

                if ((file.attr & 0x08) && !(file.attr & 0x10)) continue; // skip volume label

                printf("%s %s [%u bytes]\n", file.is_dir ? "[DIR]" : "[FILE]", file.name, file.size);
            }
        }

        cluster = get_fat_entry(cluster);
    }
}

bool fat32_read_file(const char* path, uint8_t* buffer, uint32_t max_len, uint32_t* out_len) {
    fat32_dir_entry_t file;
    if (!fat32_find_entry(path, &file)) return false;
    if (file.is_dir) return false;

    uint32_t cluster = file.first_cluster;
    uint32_t remaining = file.size;
    uint8_t* out_ptr = buffer;

    if (out_len) *out_len = 0;

    while (!is_end_of_cluster_chain(cluster) && remaining > 0) {
        uint32_t lba = get_cluster_lba(cluster);

        for (uint8_t sec = 0; sec < fs.sectors_per_cluster && remaining > 0; sec++) {
            read_sector(lba + sec);
            uint32_t to_copy = (remaining > 512) ? 512 : remaining;
            if (to_copy > max_len) to_copy = max_len;
            memcpy(out_ptr, sector, to_copy);
            out_ptr += to_copy;
            remaining -= to_copy;
            max_len -= to_copy;
            if (out_len) *out_len += to_copy;
        }

        cluster = get_fat_entry(cluster);
    }

    return true;
}

// --- Write helpers ---

static void write_dot_entries(uint32_t cluster, uint32_t parent_cluster) {
    uint32_t lba = get_cluster_lba(cluster);
    memset(sector, 0, 512);

    // '.' entry
    memcpy(&sector[0], ".       ", 8);
    sector[8] = ' ';
    sector[11] = 0x10;
    *(uint16_t*)&sector[20] = (cluster >> 16) & 0xFFFF;
    *(uint16_t*)&sector[26] = cluster & 0xFFFF;

    // '..' entry
    memcpy(&sector[32], "..      ", 8);
    sector[40] = ' ';
    sector[43] = 0x10;
    *(uint16_t*)&sector[52] = (parent_cluster >> 16) & 0xFFFF;
    *(uint16_t*)&sector[58] = parent_cluster & 0xFFFF;

    write_sector(lba, sector);
}

static uint8_t lfn_checksum(const uint8_t* short_entry) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + short_entry[i];
    }
    return sum;
}

static void write_lfn_entries(uint8_t* sector, int start_offset, const char* lfn_name, uint8_t checksum, int count) {
    int len = strlen(lfn_name);
    for (int i = 0; i < count; i++) {
        int ord = count - i;
        uint8_t* entry = sector + start_offset + i * 32;
        memset(entry, 0, 32);
        entry[0] = ord;
        if (i == 0) entry[0] |= 0x40;  // Last LFN entry

        entry[11] = 0x0F; // LFN attr
        entry[13] = checksum;

        for (int c = 0; c < 13; c++) {
            int char_index = ord * 13 - 13 + c;
            uint16_t wc = (char_index < len) ? (uint16_t)lfn_name[char_index] : 0xFFFF;

            int pos;
            if (c < 5) pos = 1 + c*2;
            else if (c < 11) pos = 14 + (c - 5)*2;
            else pos = 28 + (c - 11)*2;

            *(uint16_t*)(entry + pos) = wc;
        }
    }
}

static void generate_short_name(const char* lfn_name, uint8_t* name83) {
    memset(name83, ' ', 11);
    const char* ext = strrchr(lfn_name, '.');
    int i;

    for (i = 0; i < 8 && lfn_name[i] && lfn_name[i] != '.'; i++) {
        name83[i] = k_toupper(lfn_name[i]);
    }

    if (ext) {
        ext++;
        for (i = 0; i < 3 && ext[i]; i++) {
            name83[8 + i] = k_toupper(ext[i]);
        }
    }
}

bool fat32_write_dir_entry(uint32_t dir_cluster, const char* lfn_name, uint32_t first_cluster, uint32_t size, uint8_t attr) {
    uint8_t name83[11];
    generate_short_name(lfn_name, name83);
    uint8_t checksum = lfn_checksum(name83);
    int lfn_entries = (strlen(lfn_name) + 12) / 13;

    while (dir_cluster < 0x0FFFFFF8) {
        uint32_t lba = get_cluster_lba(dir_cluster);
        for (uint8_t sec = 0; sec < fs.sectors_per_cluster; sec++) {
            read_sector(lba + sec);

            int free_start = -1, free_count = 0;
            for (int offset = 0; offset < 512; offset += 32) {
                if (sector[offset] == 0x00 || sector[offset] == 0xE5) {
                    if (free_start == -1) free_start = offset;
                    free_count++;
                } else {
                    free_start = -1;
                    free_count = 0;
                }

                if (free_count >= lfn_entries + 1) {
                    write_lfn_entries(sector, free_start, lfn_name, checksum, lfn_entries);

                    int short_entry_offset = free_start + lfn_entries * 32;
                    memcpy(&sector[short_entry_offset], name83, 11);
                    sector[short_entry_offset + 11] = attr;
                    sector[short_entry_offset + 12] = 0;
                    *(uint16_t*)&sector[short_entry_offset + 20] = (first_cluster >> 16) & 0xFFFF;
                    *(uint16_t*)&sector[short_entry_offset + 26] = first_cluster & 0xFFFF;
                    *(uint32_t*)&sector[short_entry_offset + 28] = size;

                    write_sector(lba + sec, sector);
                    return true;
                }
            }
        }
        dir_cluster = get_fat_entry(dir_cluster);
    }

    printf("No free dir entries for '%s'\n", lfn_name);
    return false;
}

bool fat32_write_file(const char* path, const uint8_t* buffer, uint32_t len) {
    fat32_dir_entry_t file;
    if (fat32_find_entry(path, &file)) {
        printf("File exists: %s\n", path);
        return false;
    }

    uint32_t cluster_size = fs.bytes_per_sector * fs.sectors_per_cluster;
    uint32_t clusters_needed = (len + cluster_size - 1) / cluster_size;

    uint32_t first_cluster = alloc_cluster_chain(clusters_needed);
    if (!first_cluster) {
        printf("Alloc cluster failed\n");
        return false;
    }

    uint32_t cluster = first_cluster;
    uint32_t bytes_written = 0;

    while (!is_end_of_cluster_chain(cluster) && bytes_written < len) {
        uint32_t lba = get_cluster_lba(cluster);
        for (uint8_t sec = 0; sec < fs.sectors_per_cluster && bytes_written < len; sec++) {
            memset(sector, 0, 512);
            uint32_t to_write = len - bytes_written;
            if (to_write > 512) to_write = 512;
            memcpy(sector, buffer + bytes_written, to_write);
            write_sector(lba + sec, sector);
            bytes_written += to_write;
        }
        cluster = get_fat_entry(cluster);
    }

    if (!fat32_write_dir_entry(fs.root_cluster, path, first_cluster, len, 0x20)) {
        printf("Write dir entry failed\n");
        return false;
    }

    return true;
}

bool fat32_create_directory(const char* path) {
    fat32_dir_entry_t dir;
    if (fat32_find_entry(path, &dir)) {
        return false;
    }

    uint32_t first_cluster = alloc_cluster_chain(1);
    if (!first_cluster) {
        printf("Alloc cluster failed\n");
        return false;
    }

    clear_cluster(first_cluster);
    write_dot_entries(first_cluster, fs.root_cluster);

    if (!fat32_write_dir_entry(fs.root_cluster, path, first_cluster, 0, 0x10)) {
        printf("Write dir entry failed\n");
        return false;
    }

    return true;
}

bool fat32_delete_entry(const char* path) {
    fat32_dir_entry_t entry;
    if (!fat32_find_entry(path, &entry)) {
        printf("Entry not found: %s\n", path);
        return false;
    }

    // Find parent directory cluster & offset of the entry to delete
    // We'll scan the parent directory again to locate the exact sector and offset

    char dir_path[FAT32_MAX_LFN];
    strcpy(dir_path, path);

    // Find last slash position
    char* last_slash = strrchr(dir_path, '/');
    if (!last_slash) {
        // Entry in root dir
        last_slash = dir_path - 1; // so dir_path + 0 = ""
    }

    char name_only[FAT32_MAX_LFN];
    if (last_slash + 1) {
        strcpy(name_only, last_slash + 1);
    } else {
        strcpy(name_only, dir_path);
    }

    if (last_slash >= dir_path)
        *last_slash = '\0';
    else
        dir_path[0] = '\0'; // root

    uint32_t parent_cluster = (dir_path[0] == '\0') ? fs.root_cluster : 0;

    if (dir_path[0] != '\0') {
        fat32_dir_entry_t parent_dir;
        if (!fat32_find_entry(dir_path, &parent_dir) || !parent_dir.is_dir) {
            printf("Parent dir not found\n");
            return false;
        }
        parent_cluster = parent_dir.first_cluster;
    }

    // Scan parent directory clusters for the entry, mark it deleted
    uint32_t cluster = parent_cluster;
    char lfn_buf[FAT32_MAX_LFN];
    memset(lfn_buf, 0, sizeof(lfn_buf));

    while (!is_end_of_cluster_chain(cluster)) {
        uint32_t lba = get_cluster_lba(cluster);
        for (uint8_t sec = 0; sec < fs.sectors_per_cluster; sec++) {
            read_sector(lba + sec);
            bool found = false;

            for (int i = 0; i < 512; i += 32) {
                uint8_t* e = &sector[i];
                if (e[0] == 0x00) return false; // no more entries

                if (e[0] == 0xE5) continue; // deleted

                if ((e[11] & 0x0F) == 0x0F) {
                    int ord = e[0] & 0x1F;
                    parse_lfn_entry(e, lfn_buf, ord);
                    continue;
                }

                fat32_dir_entry_t current;
                parse_dir_entry(e, &current, lfn_buf);
                memset(lfn_buf, 0, sizeof(lfn_buf));

                if (stricmp(current.name, name_only) == 0) {
                    // Mark all related entries (LFNs + short entry) deleted with 0xE5
                    // Go backward to delete LFN entries too
                    int back = i;
                    while (back >= 0) {
                        if ((sector[back] & 0x0F) == 0x0F)
                            sector[back] = 0xE5;
                        else break;
                        back -= 32;
                    }
                    // Delete short entry
                    sector[i] = 0xE5;

                    write_sector(lba + sec, sector);
                    found = true;
                    break;
                }
            }

            if (found) {
                // Free clusters
                uint32_t c = entry.first_cluster;
                while (c != 0 && !is_end_of_cluster_chain(c)) {
                    uint32_t next = get_fat_entry(c);
                    set_fat_entry(c, 0);
                    c = next;
                }
                return true;
            }
        }
        cluster = get_fat_entry(cluster);
    }

    return false;
}


void fat32_flush(void) {
    // No caching implemented yet, so nothing to flush.
    // Future: flush any cached FAT sectors or directory sectors here.
}



void ls(const string_t path) {
    const char* p = path;
    if (path[0] == '/') p++; // skip leading slash

    fat32_dir_entry_t dir;
    extern string_t current_directory;
    if (path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
        // List current directory
        p = current_directory;
        if (p[0] == '/') p++; // skip leading slash
    }
    if (!fat32_find_entry(p, &dir) || !dir.is_dir) {
        printf("Not a directory: %s\n", path);
        return;
    }

    uint32_t cluster = dir.first_cluster;
    char lfn_buf[FAT32_MAX_LFN];
    memset(lfn_buf, 0, sizeof(lfn_buf));

    while (!is_end_of_cluster_chain(cluster)) {
        uint32_t lba = get_cluster_lba(cluster);

        for (uint8_t sec = 0; sec < fs.sectors_per_cluster; sec++) {
            read_sector(lba + sec);

            for (int i = 0; i < 512; i += 32) {
                uint8_t* entry = &sector[i];

                if (entry[0] == 0x00) return;
                if (entry[0] == 0xE5) continue;
                if ((entry[11] & 0x0F) == 0x0F) {
                    int ord = entry[0] & 0x1F;
                    parse_lfn_entry(entry, lfn_buf, ord);
                    continue;
                }

                fat32_dir_entry_t file;
                parse_dir_entry(entry, &file, lfn_buf);
                memset(lfn_buf, 0, sizeof(lfn_buf));

                if ((file.attr & 0x08) && !(file.attr & 0x10)) continue; // skip volume label

                printf("%s %s [%u bytes]\n", file.is_dir ? "[DIR]" : "[FILE]", file.name, file.size);
            }
        }

        cluster = get_fat_entry(cluster);
    }
}




void cat(const string_t file_path) {
    const char* path = file_path;
    if (file_path[0] == '/') {
        path++;  // skip leading slash
    }

    uint8_t buf[8192];
    uint32_t read_len = 0;

    if (fat32_read_file(path, buf, sizeof(buf) - 1, &read_len)) {
        buf[read_len] = '\0';
        printf("%s\n", (char*)buf);
    } else {
        printf("Failed to read file: %s\n", path);
    }
}


char* strrchr(const char* s, int c) {
    char* last = NULL;
    while (*s) {
        if (*s == (char)c) last = (char*)s;
        s++;
    }
    return last;
}


int stricmp(const char* s1, const char* s2) {
    while (*s1 && *s2) {
        char c1 = k_tolower(*s1);
        char c2 = k_tolower(*s2);
        if (c1 != c2) return (int)(c1 - c2);
        s1++;
        s2++;
    }
    return (int)(k_tolower(*s1) - k_tolower(*s2));
}
