#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "memory.h"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static void decoder_tests(void)
{
    uint8_t pixels[8];
    const uint8_t expected[] = {0,2,3,3,3,3,2,0}; // Pan Docs sample.
    ppu_decode_tile_row(0x3C, 0x7E, pixels);
    CHECK(memcmp(pixels, expected, 8) == 0);
    ppu_decode_tile_row(0x80, 0x01, pixels);
    CHECK(pixels[0] == 1 && pixels[7] == 2);
    for (unsigned x = 1; x < 7; ++x) CHECK(pixels[x] == 0);
    uint8_t tile[16], decoded[8][8];
    for (unsigned y = 0; y < 8; ++y) {
        tile[y * 2] = (uint8_t)(0x80u >> y);
        tile[y * 2 + 1] = (uint8_t)(1u << y);
    }
    ppu_decode_tile(tile, decoded);
    for (unsigned y = 0; y < 8; ++y)
        for (unsigned x = 0; x < 8; ++x)
            CHECK(decoded[y][x] == (uint8_t)((x == y ? 1 : 0) + (x + y == 7 ? 2 : 0)));
    // Exercise every pair of bitplanes, including all-white/all-black rows.
    for (unsigned low = 0; low < 256; ++low)
        for (unsigned high = 0; high < 256; ++high) {
            ppu_decode_tile_row((uint8_t)low, (uint8_t)high, pixels);
            for (unsigned x = 0; x < 8; ++x) {
                unsigned mask = 128u >> x;
                CHECK(pixels[x] == ((low & mask ? 1u : 0u) + (high & mask ? 2u : 0u)));
            }
        }
}

// Test fixture encoder is deliberately independent of the production decoder.
static unsigned pattern(unsigned tile, unsigned x, unsigned y)
{
    return (tile + x + 2 * y + tile / 4 + tile / 128) % 4;
}

static void seed_vram(Memory *m)
{
    for (unsigned tile = 0; tile < 384; ++tile)
        for (unsigned y = 0; y < 8; ++y) {
            uint8_t lo = 0, hi = 0;
            for (unsigned x = 0; x < 8; ++x) {
                unsigned c = pattern(tile, x, y);
                lo = (uint8_t)(lo * 2 + c % 2);
                hi = (uint8_t)(hi * 2 + c / 2);
            }
            m->vram[tile * 16 + y * 2] = lo;
            m->vram[tile * 16 + y * 2 + 1] = hi;
        }
    // Both maps differ; first row includes IDs 00, 7F, 80, FF.
    const uint8_t ids[] = {0,127,128,255};
    for (unsigned i = 0; i < 1024; ++i) {
        m->vram[0x1800 + i] = i < 4 ? ids[i] : (uint8_t)(i * 37 + i / 32);
        m->vram[0x1C00 + i] = i < 4 ? ids[3 - i] : (uint8_t)(i * 19 + 53);
    }
}

static void background_tests(void)
{
    const uint8_t scrolls[][2] = {{0,0},{1,7},{7,1},{8,8},{255,255},{249,250}};
    const uint8_t palettes[] = {0xE4,0x1B,0x55,0xFF,0x00};
    for (unsigned map = 0; map < 2; ++map)
        for (unsigned addressing = 0; addressing < 2; ++addressing)
            for (unsigned scroll = 0; scroll < sizeof(scrolls)/sizeof(scrolls[0]); ++scroll)
                for (unsigned pal = 0; pal < sizeof(palettes); ++pal) {
                    Memory m;
                    memory_init(&m); seed_vram(&m);
                    memory_write(&m, 0xFF43, scrolls[scroll][0]);
                    memory_write(&m, 0xFF42, scrolls[scroll][1]);
                    memory_write(&m, 0xFF47, palettes[pal]);
                    memory_write(&m, 0xFF40, (uint8_t)(0x81 | (map ? 8 : 0) | (addressing ? 16 : 0)));
                    memory_tick(&m, 65664);
                    CHECK(m.ppu.frame_ready && m.io[0x0F] == 1);
                    for (unsigned y = 0; y < LCD_HEIGHT; ++y)
                        for (unsigned x = 0; x < LCD_WIDTH; ++x) {
                            unsigned bx = (scrolls[scroll][0] + x) % 256;
                            unsigned by = (scrolls[scroll][1] + y) % 256;
                            unsigned id = m.vram[(map ? 0x1C00u : 0x1800u) + by / 8 * 32 + bx / 8];
                            unsigned tile = addressing || id >= 128 ? id : 256 + id;
                            unsigned color = pattern(tile, bx % 8, by % 8);
                            unsigned shade = ((unsigned)palettes[pal] >> (color * 2)) & 3u;
                            CHECK(m.ppu.framebuffer[y][x] == shade);
                        }
                }
}

static void render_boundary_tests(void)
{
    Memory m;
    memory_init(&m);
    memset(m.vram, 0xFF, 16); // Tile 0 is solid color 3.
    memory_write(&m, 0xFF47, 0xE4);
    memory_write(&m, 0xFF40, 0x91);
    memory_tick(&m, 251);
    CHECK(m.ppu.framebuffer[0][0] == 0);
    memory_tick(&m, 1);
    CHECK(m.ppu.framebuffer[0][0] == 3 && m.ppu.framebuffer[1][0] == 0);
    CHECK(m.ppu.dot == 252 && m.timer.divider == 252);
    // Raw VRAM writes affect future scanlines, never an already drawn line.
    memory_write(&m, 0x8002, 0);
    memory_write(&m, 0x8003, 0);
    memory_tick(&m, 456);
    CHECK(m.ppu.framebuffer[0][0] == 3 && m.ppu.framebuffer[1][0] == 0);
    memory_write(&m, 0xFF47, 0x1B); // Reverse palette for the next line.
    memory_tick(&m, 456);
    CHECK(m.ppu.framebuffer[2][0] == 0);
    memory_write(&m, 0xFF47, 0xFF);
    memory_write(&m, 0xFF40, 0x90); // BG off forces white, not BGP[0]=black.
    memory_tick(&m, 456);
    CHECK(m.ppu.framebuffer[3][0] == 0);
    memory_write(&m, 0xFF40, 0);
    for (unsigned y = 0; y < LCD_HEIGHT; ++y)
        for (unsigned x = 0; x < LCD_WIDTH; ++x) CHECK(m.ppu.framebuffer[y][x] == 0);
    CHECK(m.ppu.vram == m.vram); // LCD toggles preserve VRAM binding.

    // Detached timing-only instances are safe even when BG is enabled.
    PPU p;
    uint8_t flags = 0;
    ppu_init(&p); ppu_write(&p, 0xFF40, 0x91);
    ppu_tick(&p, 65664, &flags);
    CHECK(p.frame_ready && p.framebuffer[143][159] == 0);
}

static void rendering_batch_tests(void)
{
    Memory a, b;
    memory_init(&a); memory_init(&b); seed_vram(&a); seed_vram(&b);
    memory_write(&a, 0xFF47, 0xE4); memory_write(&b, 0xFF47, 0xE4);
    memory_write(&a, 0xFF40, 0x91); memory_write(&b, 0xFF40, 0x91);
    memory_tick(&a, 70224);
    for (unsigned i = 0; i < 70224; ++i) memory_tick(&b, 1);
    CHECK(memcmp(a.ppu.framebuffer, b.ppu.framebuffer, sizeof(a.ppu.framebuffer)) == 0);
    CHECK(a.ppu.dot == b.ppu.dot && a.ppu.ly == b.ppu.ly);
    CHECK(a.io[0x0F] == b.io[0x0F] && a.ppu.frame_ready == b.ppu.frame_ready);
}

int main(void)
{
    decoder_tests(); background_tests(); render_boundary_tests(); rendering_batch_tests();
    puts("PASS: 2bpp decoding, tile addressing/maps, palette, scrolling, framebuffer and rendering boundaries");
    return 0;
}
