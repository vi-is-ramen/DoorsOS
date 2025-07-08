#include "pit.h"
#include "../ps2/io.h"

void pit_init(uint32_t frequency) {
    uint32_t divisor = 1193180 / frequency;

    // Command byte: channel 0, access mode lobyte/hibyte, mode 3 (square wave)
    outb(0x43, 0x36);

    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
}


void delay_cycles(uint64_t cycles) {
    for (volatile uint64_t i = 0; i < cycles; i++) {
        // Do nothing, just waste time
    }
}

void sleep(uint32_t milliseconds) {
    // Convert milliseconds to PIT ticks
     uint64_t cycles = (uint64_t)milliseconds * 3000000;
    delay_cycles(cycles);
}

void sDelay(uint32_t seconds) {
    // Convert seconds to milliseconds
    uint32_t milliseconds = seconds * 1000;
    
    // Call the sleep function
    sleep(milliseconds);
}




