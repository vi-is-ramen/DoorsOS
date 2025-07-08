#pragma once

#include <stdint.h>
#include <stddef.h>


void pit_init(uint32_t frequency);

void sleep(uint32_t milliseconds);
void sDelay(uint32_t seconds);
