#include  "cpuinfo.h"
#include "../bootloader.h"
#include "../gfx/serial_io.h"
#include <stdint.h>

extern volatile struct limine_mp_request smp_request;

void print_cpu_info(void) {
    if (!smp_request.response) {
        serial_io_printf("SMP response not available\n");
        return;
    }

    size_t cpu_count = smp_request.response->cpu_count;
    serial_io_printf("Total CPUs detected: %zu\n", cpu_count);

    for (size_t i = 0; i < cpu_count; i++) {
        struct limine_smp_cpu *cpu = smp_request.response->cpus[i];
        serial_io_printf("CPU %zu:\n", i);
        // You can add more fields if Limine provides them
    }
}