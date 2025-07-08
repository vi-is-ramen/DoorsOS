#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct FreeBlock {
    struct FreeBlock* next;
    size_t size;
} FreeBlock;


#define malloc allocator_malloc
#define free allocator_free
#define calloc allocator_calloc
#define realloc allocator_realloc

void allocator_init(void* heap, size_t heap_size);
void* allocator_malloc(size_t size);
void allocator_free(void* ptr);

// Calloc
static inline void* allocator_realloc(void* ptr, size_t new_size);
static inline void* allocator_calloc(size_t num, size_t size);