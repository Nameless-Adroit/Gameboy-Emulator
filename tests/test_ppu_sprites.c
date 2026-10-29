#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "memory.h"
#include "graphics_trace.h"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static void object(Memory *m, unsigned index, uint8_t y, uint8_t x, uint8_t tile, uint8_t flags)
{
    uint8_t *entry = m->oam + index * 4;
    entry[0] = y; entry[1] = x; entry[2] = tile; entry[3] = flags;
}

static void solid(Memory *m, unsigned tile, unsigned color)
{
    for (unsigned y = 0; y < 8; ++y) {
        m->vram[tile * 16 + y * 2] = (color & 1) ? 255 : 0;
        m->vram[tile * 16 + y * 2 + 1] = (color & 2) ? 255 : 0;
    }
}

static void setup(Memory *m)
{
    memory_init(m);
    memory_write(m, 0xFF47, 0xE4);
    memory_write(m, 0xFF48, 0xE4);
    memory_write(m, 0xFF49, 0x1B);
    solid(m, 1, 3); solid(m, 2, 2);
}

static void selection_tests(void)
{
    Memory m;
    setup(&m);
    CHECK(m.ppu.oam == m.oam && m.ppu.oam_dma_active == &m.dma.active);
    object(&m, 0, 16, 8, 1, 0);
    object(&m, 39, 16, 24, 2, 0);
    memory_write(&m, 0xFF40, 0x93);
    memory_tick(&m, 1); CHECK(m.ppu.sprite_count == 0);
    memory_tick(&m, 1); CHECK(m.ppu.sprite_count == 1 && m.ppu.sprites[0].index == 0);
    object(&m, 0, 16, 8, 2, 0); // Selected entry is a snapshot, not a live pointer.
    memory_cpu_write(&m, 0xFE02, 1); CHECK(m.oam[2] == 2); // Mode-2 CPU write blocked.
    memory_tick(&m, 77); CHECK(m.ppu.sprite_count == 1);
    memory_tick(&m, 1); CHECK(m.ppu.sprite_count == 2 && m.ppu.sprites[1].index == 39);
    CHECK((m.ppu.stat & 3) == 3);
    // Rebinding a copied Memory fixes borrowed pointers without discarding
    // value-type Mode-2 snapshots already captured for the current scanline.
    Memory clone = m;
    ppu_attach_vram(&clone.ppu, clone.vram);
    ppu_attach_oam(&clone.ppu, clone.oam, &clone.dma.active);
    CHECK(clone.ppu.sprite_count == 2 && clone.ppu.oam == clone.oam);
    memory_tick(&clone, 172); CHECK(clone.ppu.framebuffer[0][0] == 3);
    memory_tick(&m, 172); CHECK(m.ppu.framebuffer[0][0] == 3 && m.ppu.framebuffer[0][16] == 2);
    memory_tick(&m, 456); CHECK(m.ppu.framebuffer[1][0] == 2); // Next line sees changed OAM.

    setup(&m);
    for (unsigned i = 0; i < 10; ++i) object(&m, i, 16, i & 1 ? 168 : 0, 1, 0);
    object(&m, 10, 16, 8, 1, 0);
    memory_write(&m, 0xFF40, 0x93); memory_tick(&m, 252);
    CHECK(m.ppu.sprite_count == 10 && m.ppu.framebuffer[0][0] == 0);
    for (unsigned i = 0; i < 10; ++i) CHECK(m.ppu.sprites[i].index == i);
    object(&m, 0, 0, 0, 1, 0); // Y-hidden entry frees a slot; X-hidden entries did not.
    memory_tick(&m, 456);
    CHECK(m.ppu.sprite_count == 10 && m.ppu.sprites[9].index == 10);
    CHECK(m.ppu.framebuffer[1][0] == 3);
    memory_write(&m, 0xFF40, 0); CHECK(m.ppu.sprite_count == 0);
    memory_write(&m, 0xFF40, 0x93);
    CHECK(m.ppu.sprite_count == 0); // No stale per-line selection on LCD restart.
    ppu_attach_oam(&m.ppu, NULL, NULL); memory_tick(&m, 252);
    CHECK(m.ppu.sprite_count == 0 && m.ppu.framebuffer[0][0] == 0);
}

static unsigned pattern(unsigned tile, unsigned x, unsigned y)
{
    return (tile + tile / 16 + x + 2 * y + y / 4) % 4;
}

static void geometry_tests(void)
{
    const unsigned positions[][2] = {{1,9},{7,8},{8,16},{159,150},{167,159},{168,16},{255,255}};
    for (unsigned tall = 0; tall < 2; ++tall)
        for (unsigned flip = 0; flip < 4; ++flip)
            for (unsigned palette = 0; palette < 2; ++palette)
                for (unsigned bg_signed = 0; bg_signed < 2; ++bg_signed)
                    for (unsigned pos = 0; pos < sizeof(positions)/sizeof(positions[0]); ++pos) {
                        Memory m;
                        setup(&m);
                        for (unsigned tile = 1; tile < 256; ++tile)
                            for (unsigned y = 0; y < 8; ++y) {
                                unsigned low = 0, high = 0;
                                for (unsigned x = 0; x < 8; ++x) {
                                    unsigned c = pattern(tile, x, y);
                                    low = low * 2 + c % 2; high = high * 2 + c / 2;
                                }
                                m.vram[tile * 16 + y * 2] = (uint8_t)low;
                                m.vram[tile * 16 + y * 2 + 1] = (uint8_t)high;
                            }
                        memory_write(&m, 0xFF47, 0x55); // Background shade 1.
                        memory_write(&m, 0xFF48, 0xE7); // Color zero maps black but must remain transparent.
                        unsigned raw_tile = pos & 1 ? 255u : 3u;
                        unsigned flags = (flip & 1 ? 0x20u : 0) | (flip & 2 ? 0x40u : 0) | (palette ? 0x10u : 0);
                        object(&m, 0, (uint8_t)positions[pos][1], (uint8_t)positions[pos][0], (uint8_t)raw_tile, (uint8_t)flags);
                        memory_write(&m, 0xFF40, (uint8_t)(0x83 | (tall ? 4 : 0) | (bg_signed ? 0 : 16)));
                        memory_tick(&m, 65664);
                        CHECK(m.ppu.frame_ready && m.ppu.ly == 144 && m.ppu.dot == 0);
                        for (unsigned y = 0; y < LCD_HEIGHT; ++y)
                            for (unsigned x = 0; x < LCD_WIDTH; ++x) {
                                int sx = (int)x + 8 - (int)positions[pos][0];
                                int sy = (int)y + 16 - (int)positions[pos][1];
                                unsigned expected = 1;
                                if (sx >= 0 && sx < 8 && sy >= 0 && sy < (tall ? 16 : 8)) {
                                    unsigned px = (flags & 0x20) ? 7u - (unsigned)sx : (unsigned)sx;
                                    unsigned py = (flags & 0x40) ? (tall ? 15u : 7u) - (unsigned)sy : (unsigned)sy;
                                    unsigned tile = tall ? (raw_tile & 0xFEu) + py / 8 : raw_tile;
                                    unsigned color = pattern(tile, px, py % 8);
                                    if (color) expected = ((palette ? 0x1Bu : 0xE7u) >> (color * 2)) & 3u;
                                }
                                CHECK(m.ppu.framebuffer[y][x] == expected);
                            }
                    }
}

static void priority_tests(void)
{
    Memory m;
    // Smaller X wins even if it is later in OAM. Same X uses earlier OAM.
    setup(&m); object(&m, 0, 16, 12, 1, 0); object(&m, 1, 16, 10, 2, 0);
    memory_write(&m, 0xFF40, 0x93); memory_tick(&m, 252);
    CHECK(m.ppu.framebuffer[0][4] == 2);
    setup(&m); object(&m, 0, 16, 8, 1, 0); object(&m, 1, 16, 8, 2, 0);
    memory_write(&m, 0xFF40, 0x93); memory_tick(&m, 252);
    CHECK(m.ppu.framebuffer[0][0] == 3);
    // Transparent winner pixels allow lower objects; OBP's color-0 bits do not matter.
    setup(&m); solid(&m, 1, 0); object(&m, 0, 16, 8, 1, 0); object(&m, 1, 16, 8, 2, 0);
    memory_write(&m, 0xFF48, 0xE7); memory_write(&m, 0xFF40, 0x93); memory_tick(&m, 252);
    CHECK(m.ppu.framebuffer[0][0] == 2);
    // Opaque white is still an OBJ pixel, masking a lower-priority dark object.
    setup(&m); object(&m, 0, 16, 8, 1, 0x10); object(&m, 1, 16, 8, 2, 0);
    memory_write(&m, 0xFF47, 0x55); memory_write(&m, 0xFF40, 0x93); memory_tick(&m, 252);
    CHECK(m.ppu.framebuffer[0][0] == 0);
    // CGB-only flag bits have no effect in this DMG renderer.
    setup(&m); object(&m, 0, 16, 8, 1, 0x0F);
    memory_write(&m, 0xFF40, 0x93); memory_tick(&m, 252);
    CHECK(m.ppu.framebuffer[0][0] == 3);
    // A higher-priority behind-BG object masks lower objects, rather than
    // letting them show through once the winner loses to BG.
    setup(&m); solid(&m, 0, 1); object(&m, 0, 16, 8, 1, 0x80); object(&m, 1, 16, 8, 2, 0);
    memory_write(&m, 0xFF47, 0); // Nonzero BG ID maps white; it still hides OBJ.
    memory_write(&m, 0xFF40, 0x93); memory_tick(&m, 252);
    CHECK(m.ppu.framebuffer[0][0] == 0);
    // BG color zero mapped to black does not hide a behind-BG sprite.
    setup(&m); object(&m, 0, 16, 8, 2, 0x80);
    memory_write(&m, 0xFF47, 3); memory_write(&m, 0xFF40, 0x93); memory_tick(&m, 252);
    CHECK(m.ppu.framebuffer[0][0] == 2 && m.ppu.framebuffer[0][8] == 3);
    // Window's original color ID participates in the same priority test.
    setup(&m); solid(&m, 0, 0); solid(&m, 3, 1); memset(m.vram + 0x1C00, 3, 1024);
    object(&m, 0, 16, 8, 2, 0x80); memory_write(&m, 0xFF4B, 11);
    memory_write(&m, 0xFF40, 0xF3); memory_tick(&m, 252);
    CHECK(m.ppu.framebuffer[0][3] == 2 && m.ppu.framebuffer[0][4] == 1);
    // DMG BG disable does not disable objects; OBJ enable is independent.
    setup(&m); object(&m, 0, 16, 8, 1, 0x90);
    memory_write(&m, 0xFF40, 0x82); memory_tick(&m, 252);
    CHECK(m.ppu.framebuffer[0][0] == 0); // OBP1 maps opaque color 3 to white.
    CHECK(m.ppu.sprite_count == 1);
    setup(&m); object(&m, 0, 16, 8, 2, 0x80);
    memory_write(&m, 0xFF40, 0x82); memory_tick(&m, 252);
    CHECK(m.ppu.framebuffer[0][0] == 2 && m.ppu.framebuffer[0][8] == 0);
    setup(&m); object(&m, 0, 16, 8, 1, 0);
    memory_write(&m, 0xFF40, 0x91); memory_tick(&m, 252);
    CHECK(m.ppu.framebuffer[0][0] == 0); // Selection may happen, but OBJ is disabled.
}

static void dma_and_batch_tests(void)
{
    Memory m;
    setup(&m);
    m.wram[0] = 16; m.wram[1] = 8; m.wram[2] = 1;
    memory_write(&m, 0xFF46, 0xC0); memory_tick(&m, 648);
    memory_write(&m, 0xFF40, 0x93); memory_tick(&m, 252);
    CHECK(m.ppu.sprite_count == 1 && m.ppu.framebuffer[0][0] == 3);
    setup(&m); object(&m, 0, 16, 8, 1, 0);
    m.wram[0] = 16; m.wram[1] = 8; m.wram[2] = 1;
    memory_write(&m, 0xFF46, 0xC0); memory_write(&m, 0xFF40, 0x93);
    memory_tick(&m, 1164); // DMA overlaps first two lines; third sees completed OAM.
    CHECK(m.ppu.framebuffer[0][0] == 0 && m.ppu.framebuffer[1][0] == 0);
    CHECK(m.ppu.framebuffer[2][0] == 3 && !m.dma.active);

    Memory a, b;
    setup(&a); setup(&b);
    object(&a, 0, 16, 8, 1, 0); object(&b, 0, 16, 8, 1, 0);
    memory_write(&a, 0xFF41, 0x78); memory_write(&b, 0xFF41, 0x78);
    memory_write(&a, 0xFF40, 0x93); memory_write(&b, 0xFF40, 0x93);
    memory_tick(&a, 70224 + 65664);
    for (unsigned i = 0; i < 70224 + 65664; ++i) memory_tick(&b, 1);
    CHECK(memcmp(a.ppu.framebuffer, b.ppu.framebuffer, sizeof(a.ppu.framebuffer)) == 0);
    CHECK(a.ppu.ly == b.ppu.ly && a.ppu.dot == b.ppu.dot && a.ppu.stat == b.ppu.stat);
    CHECK(a.io[0x0F] == b.io[0x0F] && a.timer.divider == b.timer.divider);
    CHECK(a.ppu.frame_ready && b.ppu.frame_ready);
}

#ifdef GB_GRAPHICS_TRACE
static void trace_test(void)
{
    Memory m;
    GraphicsTrace t;
    GraphicsTraceOptions o;
    setup(&m); solid(&m, 1, 0); m.vram[16] = 0x80;
    object(&m, 0, 56, 28, 1, 0); // Top-left at selected screen coordinate 20,40.
    graphics_trace_options_init(&o); o.enabled = true;
    FILE *out = tmpfile(); CHECK(out);
    CHECK(graphics_trace_start(&t, &m, &o, out));
    memory_write(&m, 0xFF40, 0x82); memory_tick(&m, 18492);
    CHECK(m.ppu.framebuffer[40][20] == 1);
    CHECK(fflush(out) == 0 && fseek(out, 0, SEEK_SET) == 0);
    char text[2048] = {0}; CHECK(fread(text, 1, sizeof(text) - 1, out) > 0);
    char *pixel = strstr(text, "PIXEL xy=20,40 layer=sprite oam=FE00 index=0 oam_tile=01 flags=00 row_address=8010 bytes=80,00 color=1 OBP=E4 palette=1 framebuffer=1");
    CHECK(pixel && !strstr(pixel + 1, "PIXEL xy=20,40"));
    CHECK(fclose(out) == 0);
}
#endif

int main(void)
{
    selection_tests(); geometry_tests(); priority_tests(); dma_and_batch_tests();
#ifdef GB_GRAPHICS_TRACE
    trace_test();
#endif
    puts("PASS: Mode-2 OAM selection, sprite pixels, clipping, flips, palettes, priority and DMA");
    return 0;
}
