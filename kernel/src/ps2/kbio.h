#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../libs/string.h"

// PS/2 keyboard driver header file

string_t ps2_kbio_read(string_t buffStr,size_t buffSize);

void ps2_kbio_init(void);

