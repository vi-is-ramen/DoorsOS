// serial_io.c
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>
#include "printf.h"      // for vsnprintf
#include "serial_io.h"
#include "../ps2/io.h"   // for inb, outb, etc

#define SERIAL_PORT 0x3F8  // COM1 base port

// Wait for the serial port to be ready to send a byte
static void serial_wait_for_transmit() {
    while ((inb(SERIAL_PORT + 5) & 0x20) == 0);
}

// Initialize serial port COM1
void serial_io_init(void) {
    outb(SERIAL_PORT + 1, 0x00);    // Disable all interrupts
    outb(SERIAL_PORT + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    outb(SERIAL_PORT + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
    outb(SERIAL_PORT + 1, 0x00);    // Divisor hi byte
    outb(SERIAL_PORT + 3, 0x03);    // 8 bits, no parity, one stop bit
    outb(SERIAL_PORT + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
    outb(SERIAL_PORT + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}

// Send a single character out the serial port
void serial_io_putchar(char c) {
    if (c == '\n') {
        serial_io_putchar('\r'); // CR before LF for terminals
    }
    serial_wait_for_transmit();
    outb(SERIAL_PORT, (uint8_t)c);
}

// Serial printf implementation using vsnprintf
void serial_io_printf(const char *format, ...) {
    char buffer[512]; // Adjust buffer size as needed
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    for (int i = 0; i < len; i++) {
        serial_io_putchar(buffer[i]);
    }
}
