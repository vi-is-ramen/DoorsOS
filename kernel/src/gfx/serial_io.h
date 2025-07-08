#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>

void serial_io_init(void);
void serial_io_putchar(char c);
void serial_io_printf(const char *format, ...);