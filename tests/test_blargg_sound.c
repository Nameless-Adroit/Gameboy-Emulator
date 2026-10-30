#include "cpu.h"
#include <stdio.h>

/* The runner's exit status is meaningful: a process exiting successfully
 * without a completed, signed blargg result must never count as a pass. */
int main(int argc, char **argv)
{
    Memory m; CPU cpu;
    if (argc != 2) return 1;
    memory_init(&m);
    if (!load_rom(&m.cartridge, argv[1])) return 1;
    reset_cpu(&cpu, &m);
    int result = 1;
    bool saw_running = false;
    for (unsigned long steps = 0; steps < 10000000UL; ++steps) {
        if (cpu.state == CPU_LOCKED || cpu.state == CPU_STOPPED) break;
        cpu_step(&cpu, &m);
        if (memory_read(&m, 0xA001) != 0xDE ||
            memory_read(&m, 0xA002) != 0xB0 ||
            memory_read(&m, 0xA003) != 0x61) continue;
        unsigned status = memory_read(&m, 0xA000);
        /* The ROM writes its signature before initializing status to 80.
         * RAM's initial zero during that interval is not a completed test. */
        if (status == 0x80) saw_running = true;
        if (!saw_running) continue;
        if (status >= 0x80) continue;
        printf("%s: status %02X; signature DE B0 61\n", argv[1], status);
        for (uint16_t addr = 0xA004; addr < 0xC000; ++addr) {
            uint8_t c = memory_read(&m, addr);
            if (!c) break;
            putchar(c);
        }
        putchar('\n');
        result = status == 0 ? 0 : 1;
        break;
    }
    if (result) fprintf(stderr, "Sound ROM failed, timed out or stopped: %s\n", argv[1]);
    memory_destroy(&m);
    return result;
}
