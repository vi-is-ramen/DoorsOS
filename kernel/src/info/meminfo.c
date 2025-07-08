#include "meminfo.h"
#include "../limine.h"
#include <stdbool.h>
#include <stdint.h>
#include "../gfx/printf.h"
#include "../gfx/serial_io.h"
#include "../bootloader.h"

// Extern Limine requests
extern volatile struct limine_hhdm_request hhdm_request;
extern volatile struct limine_memmap_request memmap_request;

#define PAGE_SIZE 4096

struct meminfo get_memory_info(void) {
    struct meminfo info = {0};
    info.page_size = PAGE_SIZE;

    if (!memmap_request.response) {
        serial_io_printf("No memory map from Limine.\n");
        return info;
    }

    uint64_t total_usable = 0;

    for (size_t i = 0; i < memmap_request.response->entry_count; i++) {
        struct limine_memmap_entry* e = memmap_request.response->entries[i];

        // Count both usable (1) and bootloader reclaimable (5)
        if (e->type == 1 || e->type == 5) {
            total_usable += e->length;
        }
    }

    info.total_memory = total_usable;
    info.free_memory = total_usable;
    info.used_memory = 0;
    info.available_memory = total_usable;

    return info;
}

void init_memory_management(void) {
    if (!hhdm_request.response) {
        serial_io_printf("Limine HHDM request response is NULL!\n");
        __asm__ volatile("hlt");
    }

    uint64_t hhdm_offset = hhdm_request.response->offset;

    serial_io_printf("HHDM offset: %p\n", (void*)hhdm_offset);
    printf("HHDM offset: %p\n", (void*)hhdm_offset);

    if (!memmap_request.response) {
        serial_io_printf("Limine memory map response is NULL!\n");
        __asm__ volatile("hlt");
    }

    // Print each memory region
    for (size_t i = 0; i < memmap_request.response->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap_request.response->entries[i];
        serial_io_printf("Memory region: base=%p, length=%llu, type=%u\n",
            (void*)entry->base, entry->length, entry->type);
    }

    // Optional: Print total per memory type
    uint64_t total[8] = {0};
    for (size_t i = 0; i < memmap_request.response->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap_request.response->entries[i];
        if (entry->type < 8) {
            total[entry->type] += entry->length;
        }
    }

    for (int i = 0; i < 8; i++) {
        if (total[i] > 0) {
            serial_io_printf("Memory type %d: %llu bytes (%.2f MiB)\n",
                i, total[i], total[i] / (1024.0 * 1024.0));
        }
    }
}

uint64_t calculate_total_memory(void) {
    if (!memmap_request.response) {
        return 0; // No memory map available
    }

    uint64_t total = 0;
    for (size_t i = 0; i < memmap_request.response->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap_request.response->entries[i];
        total += entry->length;
    }

    return total;
}