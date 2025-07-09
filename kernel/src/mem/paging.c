#include "paging.h"
#include "../gfx/printf.h"
#include "../bootloader.h"
#include "new/pmm.h"


static PageTable* pml4;
extern volatile struct limine_memmap_request memmap_request;
extern volatile struct limine_hhdm_request hhdm_request;

void flushTLB(void* page) {
    __asm__ volatile ("invlpg (%0)" :: "r" (page) : "memory");
}

uint64_t readCR3(void) {
    uint64_t val;
    __asm__ volatile ( "mov %%cr3, %0" : "=r"(val) );
    return val;
}

PageTable* initPML4() {
    uintptr_t cr3 = (uintptr_t) readCR3();
    pml4 = (PageTable*) ((cr3 >> 12) << 12);
    return pml4;
}

void setPageTableEntry(PageEntry* entry, uint8_t flags, uintptr_t physical_address, uint16_t available) {
    entry->present                 = (flags >> 0) & 1;
    entry->writable                = (flags >> 1) & 1;
    entry->user_accessible        = (flags >> 2) & 1;
    entry->write_through_caching  = (flags >> 3) & 1;
    entry->disable_cache          = (flags >> 4) & 1;
    entry->null                   = (flags >> 5) & 1;
    entry->global                 = (flags >> 6) & 1;
    entry->avl1                   = available & 0x3;
    entry->physical_address       = physical_address;
    entry->avl2                   = available >> 3;
    entry->no_execute             = (flags >> 7) & 1;
}

void* getPhysicalAddress(void* virtual_address) {
    uintptr_t address = (uintptr_t) virtual_address;

    uint64_t offset = address & 0xFFF;
    uint64_t pml4_index = (address >> 39) & 0x1FF;
    uint64_t pdpt_index = (address >> 30) & 0x1FF;
    uint64_t pd_index   = (address >> 21) & 0x1FF;
    uint64_t pt_index   = (address >> 12) & 0x1FF;

    PageTable* pdpt = (PageTable*) ((uint64_t)(pml4->entries[pml4_index].physical_address) << 12);
    PageTable* pd   = (PageTable*) ((uint64_t)(pdpt->entries[pdpt_index].physical_address) << 12);
    PageTable* pt   = (PageTable*) ((uint64_t)(pd->entries[pd_index].physical_address) << 12);

    return (void*) ((pt->entries[pt_index].physical_address << 12) + offset);
}

static void allocateEntry(PageTable* table, size_t index, uint8_t flags) {
    void* physical_address = (void*) PhysicalAllocate(1);
    if (!physical_address) {
        printf("Failed to allocate page table\n");
        return;
    }

    uintptr_t hhdm = hhdm_request.response->offset;
    memset((void*)(hhdm + (uintptr_t)physical_address), 0, 4096);


    setPageTableEntry(&table->entries[index], flags, (uintptr_t)physical_address >> 12, 0);
}

void mapPage(void* virtual_address, void* physical_address, uint8_t flags) {
    uintptr_t va = (uintptr_t) virtual_address;
    uintptr_t pa = (uintptr_t) physical_address;

    uint64_t pml4_index = (va >> 39) & 0x1FF;
    uint64_t pdpt_index = (va >> 30) & 0x1FF;
    uint64_t pd_index   = (va >> 21) & 0x1FF;
    uint64_t pt_index   = (va >> 12) & 0x1FF;

    if (!pml4->entries[pml4_index].present)
        allocateEntry(pml4, pml4_index, flags);

    PageTable* pdpt = (PageTable*)((uint64_t)(pml4->entries[pml4_index].physical_address) << 12);

    if (!pdpt->entries[pdpt_index].present)
        allocateEntry(pdpt, pdpt_index, flags);

    PageTable* pd = (PageTable*)((uint64_t)(pdpt->entries[pdpt_index].physical_address) << 12);

    if (!pd->entries[pd_index].present)
        allocateEntry(pd, pd_index, flags);

    PageTable* pt = (PageTable*)((uint64_t)(pd->entries[pd_index].physical_address) << 12);

    setPageTableEntry(&pt->entries[pt_index], flags, pa >> 12, 0);
    flushTLB(virtual_address);
}
