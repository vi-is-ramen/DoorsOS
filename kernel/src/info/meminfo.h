#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Memory information structure
struct meminfo {
    uint64_t total_memory;      // Total memory in bytes
    uint64_t free_memory;       // Free memory in bytes
    uint64_t used_memory;       // Used memory in bytes
    uint64_t available_memory;  // Available memory in bytes
    size_t page_size;           // Size of a memory page in bytes
};

// Function to get memory information
struct meminfo get_memory_info(void);
// Function to initialize memory management
void init_memory_management(void);
uint64_t calculate_total_memory(void);