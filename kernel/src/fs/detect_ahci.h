#include "pci.h"
#include "../mem/paging.h"  // for mapPage, flushTLB, etc.
#include "ahci.h"

typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint32_t abar; // AHCI Base Memory Address (BAR5)
} AHCI_Controller;

static AHCI_Controller detected_ahci = {0};
static bool ahci_found = false;

static inline void ahci_init(volatile uint32_t* ahci_base);
static inline void map_mmio(uintptr_t vaddr, uintptr_t paddr, size_t size, uint8_t flags);

// Callback for pci_enumerate, looks for AHCI class device
static inline void ahci_pci_callback(uint8_t bus, uint8_t device, uint8_t function, pci_device_t* dev) {
    // AHCI class = 0x01 (Mass Storage), subclass = 0x06 (SATA controller), prog IF = 0x01 (AHCI 1.0)
    if (dev->class_code == 0x01 && dev->subclass == 0x06 && dev->prog_if == 0x01) {
        detected_ahci.bus = bus;
        detected_ahci.device = device;
        detected_ahci.function = function;
        detected_ahci.abar = dev->bar[5] & PCI_BAR_MEM_MASK; // BAR5
        ahci_found = true;
    }
}

bool detectAHCIDrive(AHCI_Controller* out) {
    ahci_found = false;
    pci_enumerate(ahci_pci_callback);

    if (ahci_found) {
        if (out)
            *out = detected_ahci;
        return true;
    }

    return false;
}

#define AHCI_VIRT_BASE 0x3FF7D000

volatile HBA_MEM* check_ahci_controller() {
    AHCI_Controller ahci;

    if (detectAHCIDrive(&ahci)) {
        printf("AHCI controller found at PCI %02x:%02x.%x\n", ahci.bus, ahci.device, ahci.function);
        printf("AHCI Base Address Register (ABAR): 0x%08x\n", ahci.abar);
        serial_io_printf("AHCI Base Address Register (ABAR): 0x%08x\n", ahci.abar);
        serial_io_printf("AHCI controller found at PCI %02x:%02x.%x\n", ahci.bus, ahci.device, ahci.function);
        // Map ABAR physical address to virtual address
        printf("NOw gonna map\n");
        map_mmio(AHCI_VIRT_BASE, ahci.abar, 0x1100, PAGE_PRESENT | PAGE_WRITE | PAGE_NO_EXECUTE);

        // Return virtual address pointer
        return (volatile HBA_MEM*) AHCI_VIRT_BASE;
    } else {
        printf("No AHCI controller detected on PCI bus.\n");
        return NULL;
    }
}

// MMIO mapping (virtual to physical)
static inline void map_mmio(uintptr_t vaddr, uintptr_t paddr, size_t size, uint8_t flags) {
    for (size_t offset = 0; offset < size; offset += 0x1000) {
        mapPage((void*)(vaddr + offset), (void*)(paddr + offset), flags);
    }
}

// AHCI MMIO initialization
static inline void ahci_init(volatile uint32_t* ahci_base) {
    uint32_t version = ahci_base[0x10 / 4];
    printf("AHCI Version: %u.%u\n", (version >> 16) & 0xFFFF, version & 0xFFFF);

    uint32_t cap = ahci_base[0x00 / 4];
    printf("AHCI Capabilities: 0x%08x\n", cap);

    uint32_t ghc = ahci_base[0x04 / 4];
    ahci_base[0x04 / 4] = ghc | (1 << 31);  // Enable AHCI mode
}
