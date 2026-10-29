#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
int main(int argc, char **argv)
{
    CHECK(argc == 2);
    Memory m; CPU cpu; memory_init(&m); reset_cpu(&cpu, &m);
    CHECK(load_rom(&m.cartridge, argv[1]));
    unsigned heard = 0, enabled = 0;
    for (unsigned steps = 0; steps < 2500000 && heard != 15; ++steps) {
        CHECK(cpu_step(&cpu, &m) > 0);
        unsigned status = memory_read(&m, 0xFF26) & 15u;
        enabled |= status;
        int16_t pcm[64 * 2];
        size_t count = apu_read_samples(&m.apu, pcm, 64);
        for (size_t i = 0; i < count; ++i)
            if (pcm[i * 2u] || pcm[i * 2u + 1u]) heard |= status;
    }
    CHECK(enabled == 15 && heard == 15);
    CHECK(m.ppu.frame_ready && m.ppu.framebuffer[0][0] == 3);
    puts("Audio diagnostic exercised all four channels through CPU/bus/PCM");
    return 0;
}
