#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "memory.h"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static unsigned pattern(unsigned tile, unsigned x, unsigned y)
{
    return (tile + tile / 128 + x + 2 * y + y / 4) % 4;
}

static void setup(Memory *m)
{
    memory_init(m);
    for (unsigned tile = 0; tile < 384; ++tile)
        for (unsigned y = 0; y < 8; ++y) {
            unsigned low = 0, high = 0;
            for (unsigned x = 0; x < 8; ++x) {
                unsigned c = pattern(tile, x, y);
                low = low * 2 + c % 2; high = high * 2 + c / 2;
            }
            m->vram[tile * 16 + y * 2] = (uint8_t)low;
            m->vram[tile * 16 + y * 2 + 1] = (uint8_t)high;
        }
    for (unsigned i = 0; i < 1024; ++i) {
        m->vram[0x1800 + i] = (uint8_t)(i * 7 + 3);
        m->vram[0x1C00 + i] = (uint8_t)(i * 11 + 128);
    }
    memory_write(m, 0xFF47, 0xE4);
}

static void composition_tests(void)
{
    const unsigned xs[] = {0,1,6,7,8,23,165,166,167,255};
    const unsigned ys[] = {0,5,143,144,255};
    for (unsigned bgmap = 0; bgmap < 2; ++bgmap)
        for (unsigned winmap = 0; winmap < 2; ++winmap)
            for (unsigned addressing = 0; addressing < 2; ++addressing)
                for (unsigned xi = 0; xi < sizeof(xs)/sizeof(xs[0]); ++xi)
                    for (unsigned yi = 0; yi < sizeof(ys)/sizeof(ys[0]); ++yi)
                        for (unsigned scroll = 0; scroll < 2; ++scroll) {
                            Memory m;
                            setup(&m);
                            unsigned scx = scroll ? 251u : 3u, scy = scroll ? 253u : 9u;
                            unsigned palette = scroll ? 0x1Bu : 0xE4u;
                            memory_write(&m, 0xFF43, (uint8_t)scx);
                            memory_write(&m, 0xFF42, (uint8_t)scy);
                            memory_write(&m, 0xFF47, (uint8_t)palette);
                            memory_write(&m, 0xFF4A, (uint8_t)ys[yi]);
                            memory_write(&m, 0xFF4B, (uint8_t)xs[xi]);
                            memory_write(&m, 0xFF40, (uint8_t)(0xA1 | (bgmap ? 8 : 0) |
                                (winmap ? 64 : 0) | (addressing ? 16 : 0)));
                            memory_tick(&m, 65664);
                            CHECK(m.ppu.frame_ready && m.ppu.ly == 144 && m.ppu.dot == 0);
                            CHECK((memory_read(&m, 0xFF41) & 3) == 1 && m.io[0x0F] == 1);
                            unsigned lines = xs[xi] <= 166 && ys[yi] < 144 ? 144 - ys[yi] : 0;
                            CHECK(m.ppu.window_line == lines && !m.ppu.window_y_triggered);
                            for (unsigned y = 0; y < LCD_HEIGHT; ++y)
                                for (unsigned x = 0; x < LCD_WIDTH; ++x) {
                                    int left = (int)xs[xi] - 7;
                                    bool win = xs[xi] <= 166 && y >= ys[yi] && (int)x >= left;
                                    unsigned px = win ? (unsigned)((int)x - left) : (x + scx) % 256;
                                    unsigned py = win ? y - ys[yi] : (y + scy) % 256;
                                    unsigned map = (win ? winmap : bgmap) ? 0x1C00u : 0x1800u;
                                    unsigned id = m.vram[map + py / 8 * 32 + px / 8];
                                    unsigned tile = addressing || id >= 128 ? id : 256 + id;
                                    unsigned color = pattern(tile, px % 8, py % 8);
                                    CHECK(m.ppu.framebuffer[y][x] == ((palette >> (color * 2)) & 3u));
                                }
                        }
}

static void counter_tests(void)
{
    Memory m;
    setup(&m);
    memory_write(&m, 0xFF4A, 2); memory_write(&m, 0xFF4B, 7);
    memory_write(&m, 0xFF40, 0xF1);
    memory_tick(&m, 456 * 2);
    CHECK(m.ppu.window_line == 0 && m.ppu.window_y_triggered);
    memory_tick(&m, 456); CHECK(m.ppu.window_line == 1);
    memory_write(&m, 0xFF4B, 167); // No visible pixels: pause.
    memory_tick(&m, 456); CHECK(m.ppu.window_line == 1);
    memory_write(&m, 0xFF40, 0xD1); // Window disabled, WY condition stays latched on DMG.
    memory_write(&m, 0xFF4B, 7);
    memory_tick(&m, 456); CHECK(m.ppu.window_line == 1);
    memory_write(&m, 0xFF40, 0xF0); // BG bit 0 disables both BG and Window.
    memory_tick(&m, 456); CHECK(m.ppu.window_line == 1 && m.ppu.framebuffer[5][0] == 0);
    memory_write(&m, 0xFF40, 0xF1); ppu_attach_vram(&m.ppu, NULL);
    memory_tick(&m, 456); CHECK(m.ppu.window_line == 1);
    ppu_attach_vram(&m.ppu, m.vram);
    memory_write(&m, 0xFF4A, 200); // WY writes cannot undo this frame's match.
    memory_tick(&m, 456);
    unsigned id = m.vram[0x1C00];
    CHECK(m.ppu.window_line == 2 && m.ppu.framebuffer[7][0] == pattern(id, 0, 1));
    memory_write(&m, 0xFF40, 0);
    CHECK(m.ppu.window_line == 0 && !m.ppu.window_y_triggered);
    memory_write(&m, 0xFF4A, 0); memory_write(&m, 0xFF40, 0xF1);
    memory_tick(&m, 65664); CHECK(m.ppu.window_line == 144);
    memory_tick(&m, 456 * 10);
    CHECK(m.ppu.window_line == 0 && m.ppu.window_y_triggered);
    memory_tick(&m, 252); CHECK(m.ppu.window_line == 1);
}

static void wy_latch_and_opaque_tests(void)
{
    Memory m;
    setup(&m);
    memory_write(&m, 0xFF4A, 1); memory_write(&m, 0xFF4B, 7);
    memory_write(&m, 0xFF40, 0xF1);
    memory_tick(&m, 1);
    memory_write(&m, 0xFF4A, 0); // Matching LY after line start is too late.
    memory_tick(&m, 65663);
    CHECK(m.ppu.window_line == 0);
    memory_tick(&m, 456 * 10 + 252);
    CHECK(m.ppu.window_line == 1); // Next frame sees WY=0 at line start.

    // Matching WY is independent of Window/BG enable and visible WX.
    setup(&m);
    memory_write(&m, 0xFF4A, 2); memory_write(&m, 0xFF4B, 200);
    memory_write(&m, 0xFF40, 0x80);
    memory_tick(&m, 456 * 3);
    CHECK(m.ppu.window_y_triggered && m.ppu.window_line == 0);
    memory_write(&m, 0xFF4B, 7); memory_write(&m, 0xFF40, 0xF1);
    memory_tick(&m, 252);
    CHECK(m.ppu.window_line == 1 && m.ppu.framebuffer[3][0] == pattern(128, 0, 0));

    setup(&m);
    memset(m.vram, 0xFF, 16); // BG tile 0 black.
    memset(m.vram + 16, 0, 16); // Window tile 1 color zero, opaque white.
    memset(m.vram + 0x1800, 0, 1024);
    memset(m.vram + 0x1C00, 1, 1024);
    memory_write(&m, 0xFF4B, 15); // Start at x=8.
    memory_write(&m, 0xFF40, 0xF1);
    memory_tick(&m, 252);
    CHECK(m.ppu.framebuffer[0][7] == 3 && m.ppu.framebuffer[0][8] == 0);
    CHECK(m.ppu.window_line == 1); // White pixels still count as rendering.
}

static void timing_and_batch_tests(void)
{
    Memory a, b;
    setup(&a); setup(&b);
    memory_write(&a, 0xFF4B, 3); memory_write(&b, 0xFF4B, 3);
    memory_write(&a, 0xFF41, 0x78); memory_write(&b, 0xFF41, 0x78);
    memory_write(&a, 0xFF40, 0xF1); memory_write(&b, 0xFF40, 0xF1);
    memory_tick(&a, 70224 + 65664);
    for (unsigned i = 0; i < 70224 + 65664; ++i) memory_tick(&b, 1);
    CHECK(memcmp(a.ppu.framebuffer, b.ppu.framebuffer, sizeof(a.ppu.framebuffer)) == 0);
    CHECK(a.ppu.ly == b.ppu.ly && a.ppu.dot == b.ppu.dot);
    CHECK(a.ppu.stat == b.ppu.stat && a.io[0x0F] == b.io[0x0F]);
    CHECK(a.timer.divider == b.timer.divider && a.ppu.window_line == b.ppu.window_line);
    // Window must not extend mode 3 in the existing fixed-mode timing model.
    setup(&a); memory_write(&a, 0xFF4B, 7); memory_write(&a, 0xFF40, 0xF1);
    memory_tick(&a, 80); CHECK((a.ppu.stat & 3) == 3);
    memory_tick(&a, 171); CHECK((a.ppu.stat & 3) == 3 && a.ppu.window_line == 0);
    memory_tick(&a, 1); CHECK((a.ppu.stat & 3) == 0 && a.ppu.window_line == 1);
}

int main(void)
{
    composition_tests(); counter_tests(); wy_latch_and_opaque_tests(); timing_and_batch_tests();
    puts("PASS: Window maps, positions/clipping, palette, composition, WY latch, counter and timing");
    return 0;
}
