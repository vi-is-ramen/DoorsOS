#include "pmm.h"
#include <stddef.h>
#include <stdint.h>
#include "../../libs/string.h"  // for memset
#include "bitmap.h"
#include "../../bootloader.h"
// You must define or get these externally (from bootloader or limine)
extern volatile struct limine_memmap_request memmap_request;
extern volatile struct limine_hhdm_request hhdm_request;

DS_Bitmap physical;
static size_t total_blocks = 0;

// This is the bitmap that tracks free/used physical blocks/pages
// physical.Bitmap must point to memory reserved for the bitmap itself (inside PMM or preallocated)

void initiatePMM() {
    size_t mem_start = (size_t)(-1);
    size_t mem_end = 0;
    size_t usable_size = 0;

    // Find lowest mem_start and highest mem_end among usable regions
    for (size_t i = 0; i < memmap_request.response->entry_count; i++) {
        struct limine_memmap_entry* e = memmap_request.response->entries[i];
        if (e->type == 1) {
            if ((size_t)e->base < mem_start)
                mem_start = (size_t)e->base;
            size_t end = (size_t)e->base + e->length;
            if (end > mem_end)
                mem_end = end;
            usable_size += e->length;
        }
    }

    total_blocks = usable_size / BLOCK_SIZE;
    physical.mem_start = mem_start;

    // Calculate bitmap size in bytes
    size_t bitmap_bytes = BitmapCalculateSize(usable_size);

    // Align bitmap size to BLOCK_SIZE (4096)
    size_t bitmap_blocks = (bitmap_bytes + BLOCK_SIZE - 1) / BLOCK_SIZE;
    size_t bitmap_size_aligned = bitmap_blocks * BLOCK_SIZE;

    // Place bitmap at HHDM offset + mem_start, aligned
    uintptr_t hhdm_base = (uintptr_t)hhdm_request.response->offset;
    physical.Bitmap = (uint8_t*)(hhdm_base + mem_start);

    // Mark bitmap area as reserved immediately
    MarkBlocks(&physical, 0, bitmap_blocks, true);

    // Zero bitmap after marking bitmap blocks reserved
    memset(physical.Bitmap, 0, bitmap_bytes);

    physical.BitmapSizeInBytes = bitmap_bytes;
    physical.BitmapSizeInBlocks = total_blocks;
    physical.lastDeepFragmented = 0;
    physical.allocatedSizeInBlocks = bitmap_blocks; // bitmap blocks reserved
    physical.ready = true;

    // Mark all blocks initially free except bitmap blocks
    MarkBlocks(&physical, bitmap_blocks, total_blocks - bitmap_blocks, false);

    // Mark unusable regions as used
    for (size_t i = 0; i < memmap_request.response->entry_count; i++) {
        struct limine_memmap_entry* e = memmap_request.response->entries[i];
        if (e->type != 1) {
            // Only mark blocks within usable memory range
            if ((size_t)e->base + e->length <= mem_start)
                continue;
            size_t start_block = 0;
            if ((size_t)e->base <= mem_start)
                start_block = 0;
            else
                start_block = ((size_t)e->base - mem_start) / BLOCK_SIZE;

            size_t blocks = e->length / BLOCK_SIZE;

            // Clip blocks if overflow
            if (start_block + blocks > total_blocks)
                blocks = total_blocks - start_block;

            MarkBlocks(&physical, start_block, blocks, true);
        }
    }

    debugf("PMM initialized: total blocks %zu, bitmap size %zu bytes, bitmap reserved blocks %zu\n",
           total_blocks, bitmap_bytes, bitmap_blocks);
}


size_t PhysicalAllocate(int pages) {
    void* ptr = BitmapAllocate(&physical, pages);
    if (!ptr) {
        return INVALID_BLOCK; // or 0 for failure
    }
    return (size_t) ptr;
}

void PhysicalFree(size_t ptr, int pages) {
    BitmapFree(&physical, (void*) ptr, pages);
}
