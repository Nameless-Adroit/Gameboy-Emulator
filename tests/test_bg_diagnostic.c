#include <stdio.h>
#include <stdlib.h>
#include "cpu.h"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static unsigned expected_shade(unsigned x, unsigned y)
{
    unsigned tx = x / 8, ty = y / 8;
    if (tx == 0 || tx == 19 || ty == 0 || ty == 17) return 3;
    if (ty <= 5) {
        unsigned bar = (tx - 1) / 5;
        return bar < 3 ? bar : 3;
    }
    if (ty <= 11) return (tx + ty) % 2 ? 3u : 0u;
    return x % 8 == y % 8 || x % 8 + y % 8 == 7 ? 3u : 1u;
}

int main(int argc, char **argv)
{
    CHECK(argc == 2 || argc == 3);
    Memory m;
    CPU c;
    memory_init(&m); reset_cpu(&c, &m);
    CHECK(load_rom(&m.cartridge, argv[1]));
    for (unsigned step = 0; step < 200000 && !m.ppu.frame_ready; ++step) {
        CHECK(c.state != CPU_LOCKED && c.state != CPU_STOPPED);
        cpu_step(&c, &m);
    }
    CHECK(m.ppu.frame_ready && m.ppu.ly == 144 && c.state == CPU_HALTED);
    for (unsigned y = 0; y < LCD_HEIGHT; ++y)
        for (unsigned x = 0; x < LCD_WIDTH; ++x)
            CHECK(m.ppu.framebuffer[y][x] == expected_shade(x, y));
    if (argc == 3) {
        FILE *image = fopen(argv[2], "wb");
        CHECK(image != NULL);
        CHECK(fprintf(image, "P5\n%d %d\n255\n", LCD_WIDTH, LCD_HEIGHT) > 0);
        for (unsigned y = 0; y < LCD_HEIGHT; ++y)
            for (unsigned x = 0; x < LCD_WIDTH; ++x)
                CHECK(fputc(255 - m.ppu.framebuffer[y][x] * 85, image) != EOF);
        CHECK(fclose(image) == 0);
    }
    puts("PASS: diagnostic ROM executed by CPU and all 23040 framebuffer pixels match");
    return 0;
}
