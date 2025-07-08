#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "../ps2/kbio.h"

#define FAT32_MAX_LFN 256

static inline string_t current_directory = "/";
typedef struct {
    uint32_t start_lba;
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t num_fats;
    uint32_t fat_size_sectors;
    uint32_t root_cluster;
} fat32_info_t;

typedef struct {
    char name[FAT32_MAX_LFN];
    uint8_t attr;
    uint32_t first_cluster;
    uint32_t size;
    bool is_dir;
} fat32_dir_entry_t;

// Mount partition starting at LBA `lba`
bool fat32_mount(uint32_t lba,bool ahci);

// List files/directories in root directory
void fat32_list_root(void);

// Read file at full path (e.g. "EFI/kernel.elf")
// buffer size: max_len
// out_len returns actual bytes read
bool fat32_read_file(const char* path, uint8_t* buffer, uint32_t max_len, uint32_t* out_len);

// Write `len` bytes from `buffer` into file at `path`
// If file exists, it is NOT overwritten currently (can be extended)
bool fat32_write_file(const char* path, const uint8_t* buffer, uint32_t len);

// Create an empty directory at the specified path (e.g. "NEW_DIR")
bool fat32_create_directory(const char* path);

// Delete file or directory by path
bool fat32_delete_entry(const char* path);



// Flush dirty FAT or FS metadata (if needed)
void fat32_flush(void);

// List dir
void ls(const string_t path);

// Cat function
void cat(const string_t file_path);

// Utility functions
char* strrchr(const char* s, int c);
int stricmp(const char* s1, const char* s2);
