#include "pci.h"
#include "../ps2/io.h"  // for outl, inl

// Read 16-bit word from PCI config space
uint16_t pciConfigReadWord(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address;
    uint32_t lbus = (uint32_t)bus;
    uint32_t lslot = (uint32_t)slot;
    uint32_t lfunc = (uint32_t)func;
    uint16_t ret;

    // Construct address as per PCI spec
    address = (uint32_t)((lbus << 16) | (lslot << 11) |
                        (lfunc << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));

    outl(0xCF8, address);
    ret = (uint16_t)((inl(0xCFC) >> ((offset & 2) * 8)) & 0xFFFF);
    return ret;
}

// Enable MSI for a PCI device (stub example, real MSI enable needs PCI capabilities parsing)
void enablePCIInterrupts(uint8_t bus, uint8_t device, uint8_t function, size_t ioapicaddr) {
    // This is complex and hardware-specific.
    // Typically involves reading MSI capability in PCI config space,
    // writing MSI message address and data registers.

    // Stub: Print message for now.
    // You can implement MSI capability parsing later.
    (void)bus; (void)device; (void)function; (void)ioapicaddr;
    // printf("[PCI] enablePCIInterrupts called - implement MSI parsing here\n");
}

// Check if MSI capability is supported for PCI device (stub)
void checkMSI(uint8_t bus, uint8_t device, uint8_t func) {
    // You would parse PCI capabilities list here for MSI capability (0x05).
    // This requires reading PCI config space at offset 0x34 to find capabilities pointer.
    // Then walk capabilities list and check for MSI.

    (void)bus; (void)device; (void)func;
    // printf("[PCI] checkMSI called - implement capability list parsing\n");
}

uint32_t read_pci_bar5(uint8_t bus, uint8_t slot, uint8_t func) {
    uint16_t bar5_low = pciConfigReadWord(bus, slot, func, 0x24);
    uint16_t bar5_high = pciConfigReadWord(bus, slot, func, 0x26);
    uint32_t bar5 = ((uint32_t)bar5_high << 16) | bar5_low;
    // Mask lower 4 bits (flags)
    return bar5 & 0xFFFFFFF0;
}

#define PCI_CLASS_MASS_STORAGE 0x01
#define PCI_SUBCLASS_SATA      0x06
#define PCI_PROG_IF_AHCI       0x01

void find_ahci_controller_and_set_host() {
    for (uint8_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint8_t prog_if = (pciConfigReadWord(bus, slot, func, 0x09)) & 0xFF;
                uint8_t subclass = (pciConfigReadWord(bus, slot, func, 0x0A)) & 0xFF;
                uint8_t class = (pciConfigReadWord(bus, slot, func, 0x0B)) & 0xFF;

                if (class == 0x01 && subclass == 0x06 && prog_if == 0x01) {
                    uint32_t mmio_base = read_pci_bar5(bus, slot, func);
                    host = (HBA_MEM*)(uintptr_t)mmio_base;
                    return; // Found and set host
                }
            }
        }
    }
    host = NULL; // Not found
}
