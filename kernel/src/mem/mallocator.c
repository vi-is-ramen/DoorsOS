#include <stdint.h>
#include <stddef.h>
#include "heap.h"
// Simple free list node

static uint8_t* heap_start = NULL;
static uint8_t* heap_end = NULL;

static FreeBlock* free_list = NULL;

void allocator_init(void* heap, size_t heap_size) {
    heap_start = (uint8_t*)heap;
    heap_end = heap_start + heap_size;

    free_list = (FreeBlock*)heap_start;
    free_list->size = heap_size;
    free_list->next = NULL;
}

// Align size to 8 bytes for 64-bit arch
static inline size_t align8(size_t size) {
    return (size + 7) & ~7;
}

void* allocator_malloc(size_t size) {
    size = align8(size);

    FreeBlock **current = &free_list;
    while (*current) {
        if ((*current)->size >= size + sizeof(FreeBlock)) {
            // Enough space found: split block
            FreeBlock* allocated = *current;
            size_t remaining = allocated->size - size - sizeof(FreeBlock);

            if (remaining >= sizeof(FreeBlock) + 8) {
                // Split block
                FreeBlock* new_block = (FreeBlock*)((uint8_t*)allocated + sizeof(FreeBlock) + size);
                new_block->size = remaining;
                new_block->next = allocated->next;

                allocated->size = size;
                *current = new_block;
            } else {
                // Use entire block
                *current = allocated->next;
            }
            return (void*)((uint8_t*)allocated + sizeof(FreeBlock));
        }
        current = &(*current)->next;
    }
    // No suitable block found
    return NULL;
}

void allocator_free(void* ptr) {
    if (!ptr) return;
    FreeBlock* block = (FreeBlock*)((uint8_t*)ptr - sizeof(FreeBlock));

    FreeBlock **current = &free_list;

    // Insert block back into free list sorted by address for merging
    while (*current && *current < block) {
        current = &(*current)->next;
    }

    block->next = *current;
    *current = block;

    // Try to merge with next
    if (block->next && (uint8_t*)block + sizeof(FreeBlock) + block->size == (uint8_t*)block->next) {
        block->size += sizeof(FreeBlock) + block->next->size;
        block->next = block->next->next;
    }

    // Try to merge with previous
    if (current != &free_list) {
        FreeBlock* prev = free_list;
        while (prev->next != block) {
            prev = prev->next;
        }
        if ((uint8_t*)prev + sizeof(FreeBlock) + prev->size == (uint8_t*)block) {
            prev->size += sizeof(FreeBlock) + block->size;
            prev->next = block->next;
        }
    }
}


static inline void* allocator_calloc(size_t num, size_t size) {
    size_t total_size = num * size;
    void* ptr = allocator_malloc(total_size);
    if (ptr) {
        // Zero out the allocated memory
        for (size_t i = 0; i < total_size; i++) {
            ((uint8_t*)ptr)[i] = 0;
        }
    }
    return ptr;
}

// Realloc
static inline void* allocator_realloc(void* ptr, size_t new_size) {
    if (!ptr) {
        return allocator_malloc(new_size); // If ptr is NULL, behave like malloc
    }
    
    if (new_size == 0) {
        allocator_free(ptr); // If new_size is 0, behave like free
        return NULL;
    }

    // Get the size of the current allocation (this is a placeholder, actual implementation may vary)
    size_t old_size = ((FreeBlock*)ptr - 1)->size; // Assuming FreeBlock is defined in your allocator

    if (new_size <= old_size) {
        return ptr; // No need to reallocate
    }

    void* new_ptr = allocator_malloc(new_size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, old_size); // Copy old data to new location
        allocator_free(ptr); // Free the old memory
    }
    
    return new_ptr;
}
