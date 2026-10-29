#include <stdio.h>
#include <stdlib.h>
#include "cpu.h"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static unsigned expected_shade(unsigned x, unsigned y, unsigned scroll)
{
    if (x < 24 || y < 80) return (((x + scroll) % 256) / 8 + y / 8) % 4;
    unsigned wx = x - 24, wy = y - 80;
    unsigned tx = wx / 8, ty = wy / 8;
    if (tx == 0 || tx == 16 || ty == 0 || ty == 7) return 3;
    if ((tx + ty) % 2 == 0) return 0;
    // Independent readable glyph description, not the ROM bitplane bytes.
    static const char glyph[8][9] = {
        "X......X", "X......X", "X......X", "X..XX..X",
        "X.X..X.X", "XX....XX", "X......X", "........"
    };
    return glyph[wy % 8][wx % 8] == 'X' ? 3u : 0u;
}

int main(int argc, char **argv)
{
    CHECK(argc == 2 || argc == 3);
    Memory m;
    CPU c;
    memory_init(&m); reset_cpu(&c, &m);
    CHECK(load_rom(&m.cartridge, argv[1]));
    unsigned frames = 0;
    for (unsigned step = 0; step < 500000 && frames < 3; ++step) {
        CHECK(c.state != CPU_LOCKED && c.state != CPU_STOPPED);
        cpu_step(&c, &m);
        if (!m.ppu.frame_ready) continue;
        CHECK(m.ppu.scx == frames && m.ppu.window_line == 64);
        CHECK(m.ppu.ly == 144 && m.ppu.dot == 0);
        for (unsigned y = 0; y < LCD_HEIGHT; ++y)
            for (unsigned x = 0; x < LCD_WIDTH; ++x)
                CHECK(m.ppu.framebuffer[y][x] == expected_shade(x, y, frames));
        ++frames;
        m.ppu.frame_ready = false;
    }
    CHECK(frames == 3);
    if (argc == 3) {
        FILE *image = fopen(argv[2], "wb"); CHECK(image);
        CHECK(fprintf(image, "P5\n%d %d\n255\n", LCD_WIDTH, LCD_HEIGHT) > 0);
        for (unsigned y = 0; y < LCD_HEIGHT; ++y)
            for (unsigned x = 0; x < LCD_WIDTH; ++x)
                CHECK(fputc(255 - m.ppu.framebuffer[y][x] * 85, image) != EOF);
        CHECK(fclose(image) == 0);
    }
    puts("PASS: 3 CPU-executed frames scroll BG while all Window pixels stay fixed");
    return 0;
}
