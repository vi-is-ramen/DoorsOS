#ifndef PMM_H
#define PMM_H

#include "bitmap.h"
#include <stdint.h>
#include <stddef.h>
#include "../../gfx/serial_io.h"

#define debugf serial_io_printf



void initiatePMM();

size_t PhysicalAllocate(int pages);
void   PhysicalFree(size_t ptr, int pages);

#endif