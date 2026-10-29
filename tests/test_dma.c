#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu.h"
#include "graphics_trace.h"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static uint8_t pattern(unsigned index) { return (uint8_t)(index * 29 + 3); }
static void setup(Memory *m)
{
    memory_init(m);
    memset(m->oam, 0xEE, sizeof(m->oam));
    for (unsigned i = 0; i < OAM_SIZE; ++i) {
        m->wram[i] = pattern(i);
        m->wram[0x1000 + i] = (uint8_t)(pattern(i) ^ 0xFF);
    }
}

static void progress_tests(void)
{
    Memory m;
    setup(&m);
    memory_write(&m, 0xFF46, 0xC0);
    CHECK(memory_read(&m, 0xFF46) == 0xC0 && !m.dma.active && m.dma.start_delay == 8);
    CHECK(m.oam[0] == 0xEE && m.timer.divider == 0);
    memory_tick(&m, 7); CHECK(!m.dma.active && m.oam[0] == 0xEE);
    memory_tick(&m, 1); CHECK(m.dma.active && m.dma.index == 0);
    memory_tick(&m, 3); CHECK(m.oam[0] == 0xEE);
    memory_tick(&m, 1); CHECK(m.oam[0] == pattern(0) && m.oam[1] == 0xEE);
    // The source is read progressively, not snapshotted when FF46 is written.
    memory_write(&m, 0xC001, 0xA7);
    memory_tick(&m, 4); CHECK(m.oam[1] == 0xA7 && m.dma.index == 2);
    for (unsigned i = 2; i < OAM_SIZE - 1; ++i) {
        memory_tick(&m, 4);
        CHECK(m.dma.index == i + 1 && m.oam[i] == pattern(i));
        CHECK(m.oam[i + 1] == 0xEE);
    }
    CHECK(m.timer.divider == 644 && m.dma.active && m.dma.index == 159);
    memory_tick(&m, 3); CHECK(m.dma.active && m.oam[159] == 0xEE);
    memory_tick(&m, 1);
    CHECK(!m.dma.active && m.dma.index == 160 && m.oam[159] == pattern(159));
    CHECK(m.timer.divider == 648);
    memory_write(&m, 0xC09F, 0);
    memory_tick(&m, 1000); CHECK(m.oam[159] == pattern(159)); // No overrun/repeat.
}

static void restart_tests(void)
{
    Memory m;
    setup(&m);
    memory_write(&m, 0xFF46, 0xC0);
    memory_tick(&m, 28); CHECK(m.dma.index == 5);
    memory_cpu_write(&m, 0xFF46, 0xD0); // Restart exception works while active.
    CHECK(m.dma.active && m.dma.source == 0xC000 && m.dma.index == 5);
    memory_tick(&m, 4); CHECK(m.dma.index == 6 && m.oam[5] == pattern(5));
    memory_tick(&m, 4);
    CHECK(m.dma.source == 0xD000 && m.dma.index == 0 && m.dma.active);
    CHECK(m.oam[0] == pattern(0)); // No atomic clearing/replacement.
    memory_tick(&m, 4);
    CHECK(m.oam[0] == (uint8_t)(pattern(0) ^ 0xFF) && m.oam[1] == pattern(1));
    memory_tick(&m, 636);
    CHECK(!m.dma.active);
    for (unsigned i = 0; i < OAM_SIZE; ++i) CHECK(m.oam[i] == (uint8_t)(pattern(i) ^ 0xFF));

    setup(&m);
    memory_write(&m, 0xFF46, 0xC0); memory_tick(&m, 3);
    memory_write(&m, 0xFF46, 0xD0); memory_tick(&m, 7);
    CHECK(!m.dma.active && m.oam[0] == 0xEE);
    memory_tick(&m, 641); // Latest request wins, with a fresh startup delay.
    CHECK(!m.dma.active && m.oam[0] == (uint8_t)(pattern(0) ^ 0xFF));
}

static void source_and_batch_tests(void)
{
    const uint8_t pages[] = {0x01,0x80,0xA0,0xC0,0xDF,0xE0,0xFF};
    for (unsigned p = 0; p < sizeof(pages); ++p) {
        Memory m;
        setup(&m); m.cartridge.rom_loaded = true;
        for (unsigned i = 0; i < OAM_SIZE; ++i) {
            m.cartridge.rom[0x100 + i] = pattern(i);
            m.vram[i] = pattern(i);
            m.wram[0x1F00 + i] = pattern(i);
        }
        memory_write(&m, 0xFF40, 0x80);
        memory_tick(&m, 80); // DMA can read VRAM and write OAM in mode 3.
        memory_write(&m, 0xFF46, pages[p]); memory_tick(&m, 648);
        for (unsigned i = 0; i < OAM_SIZE; ++i) {
            uint8_t expected = pages[p] == 0xA0 || pages[p] >= 0xE0 ? 0xFF : pattern(i);
            CHECK(m.oam[i] == expected);
        }
    }
    Memory a, b;
    setup(&a); setup(&b);
    memory_write(&a, 0xFF40, 0x80); memory_write(&b, 0xFF40, 0x80);
    memory_write(&a, 0xFF46, 0xC0); memory_write(&b, 0xFF46, 0xC0);
    memory_tick(&a, 651);
    for (unsigned i = 0; i < 651; ++i) memory_tick(&b, 1);
    CHECK(memcmp(a.oam, b.oam, sizeof(a.oam)) == 0);
    CHECK(a.dma.index == b.dma.index && a.dma.active == b.dma.active);
    CHECK(a.timer.divider == b.timer.divider && a.ppu.ly == b.ppu.ly && a.ppu.dot == b.ppu.dot);
    CHECK(memcmp(a.io, b.io, sizeof(a.io)) == 0);
}

static void access_tests(void)
{
    Memory m;
    setup(&m);
    m.cartridge.rom_loaded = true; m.cartridge.rom[0x100] = 0x23;
    memory_write(&m, 0xFFFF, 0x12);
    memory_write(&m, 0xFF46, 0xC0);
    memory_tick(&m, 4); CHECK(memory_cpu_read(&m, 0xC000) == pattern(0));
    memory_tick(&m, 4);
    const uint16_t blocked[] = {0x100,0x8000,0xA000,0xC000,0xE000,0xFE00,0xFF04,0xFF0F,0xFF46,0xFFFF};
    for (unsigned i = 0; i < sizeof(blocked)/sizeof(blocked[0]); ++i)
        CHECK(memory_cpu_read(&m, blocked[i]) == 0xFF);
    memory_cpu_write(&m, 0xC000, 0); CHECK(m.wram[0] == pattern(0));
    memory_cpu_write(&m, 0xFFFF, 0); CHECK(m.ie == 0x12);
    memory_cpu_write(&m, 0xFF04, 0); CHECK(m.timer.divider == 8);
    memory_cpu_write(&m, 0xFF40, 0x80); CHECK(m.ppu.lcdc == 0);
    memory_cpu_write(&m, 0x8000, 0x43); CHECK(m.vram[0] == 0);
    memory_cpu_write(&m, 0xFE00, 0x43); CHECK(m.oam[0] == 0xEE);
    memory_cpu_write(&m, 0xFF80, 0x67); memory_cpu_write(&m, 0xFFFE, 0x89);
    CHECK(memory_cpu_read(&m, 0xFF80) == 0x67 && memory_cpu_read(&m, 0xFFFE) == 0x89);
    // Raw/device access is neither restricted nor clocked.
    memory_write(&m, 0xC000, 0x42); CHECK(memory_read(&m, 0xC000) == 0x42);
    CHECK(m.timer.divider == 8 && m.dma.index == 0);
    memory_tick(&m, 640); CHECK(!m.dma.active && memory_cpu_read(&m, 0xC000) == 0x42);
}

static void ppu_access_tests(void)
{
    Memory m;
    setup(&m);
    memory_write(&m, 0x8000, 0x23); memory_write(&m, 0xFE00, 0x45);
    memory_write(&m, 0xFF40, 0x80); // Mode 2: VRAM yes, OAM no.
    CHECK(memory_cpu_read(&m, 0x8000) == 0x23 && memory_cpu_read(&m, 0xFE00) == 0xFF);
    memory_cpu_write(&m, 0xFE00, 0); CHECK(m.oam[0] == 0x45);
    memory_tick(&m, 80); // Mode 3: neither.
    CHECK(memory_cpu_read(&m, 0x8000) == 0xFF && memory_cpu_read(&m, 0xFE00) == 0xFF);
    memory_cpu_write(&m, 0x8000, 0); CHECK(m.vram[0] == 0x23);
    memory_tick(&m, 172); // Mode 0: both.
    CHECK(memory_cpu_read(&m, 0x8000) == 0x23 && memory_cpu_read(&m, 0xFE00) == 0x45);
    memory_cpu_write(&m, 0xFE00, 0x56); CHECK(m.oam[0] == 0x56);
    memory_tick(&m, 65664 - 252); // VBlank.
    CHECK(memory_cpu_read(&m, 0xFE00) == 0x56);
    memory_write(&m, 0xFF40, 0); CHECK(memory_cpu_read(&m, 0x8000) == 0x23);

    // Restriction is evaluated at the CPU access's existing end-of-M-cycle phase.
    CPU c;
    for (unsigned run = 0; run < 2; ++run) {
        setup(&m); reset_cpu(&c, &m); c.PC = 0xFF80;
        m.hram[0] = 0xFA; m.hram[1] = 0; m.hram[2] = 0x80;
        m.vram[0] = 0x23; memory_write(&m, 0xFF40, 0x80);
        memory_tick(&m, run ? 236 : 64);
        CHECK(cpu_step(&c, &m) == 16 && c.A == (run ? 0x23 : 0xFF));
        CHECK(c.total_cycles == 16 && m.ppu.dot == (run ? 252u : 80u));
    }
}

static void cpu_and_clock_tests(void)
{
    Memory m;
    CPU c;
    setup(&m); reset_cpu(&c, &m); c.PC = 0xFF80;
    const uint8_t routine[] = {0x3E,0xC0,0xE0,0x46,0x06,40,0x05,0x20,0xFD,0xFA,0x00,0xC0};
    memcpy(m.hram, routine, sizeof(routine));
    CHECK(cpu_step(&c, &m) == 8); CHECK(cpu_step(&c, &m) == 12);
    CHECK(m.dma.start_delay == 8 && !m.dma.active && m.dma.index == 0);
    CHECK(cpu_step(&c, &m) == 8 && m.dma.active);
    for (unsigned i = 0; i < 40; ++i) { cpu_step(&c, &m); cpu_step(&c, &m); }
    CHECK(m.dma.active && m.dma.index == 159);
    CHECK(cpu_step(&c, &m) == 16 && c.A == pattern(0) && !m.dma.active);
    CHECK(c.total_cycles == m.timer.divider);
    for (unsigned i = 0; i < OAM_SIZE; ++i) CHECK(m.oam[i] == pattern(i));

    setup(&m); reset_cpu(&c, &m); m.cartridge.rom_loaded = true;
    memory_write(&m, 0xFF46, 0xC0); memory_tick(&m, 8);
    CHECK(cpu_step(&c, &m) == 16 && c.PC == 0x38); // Blocked opcode fetch is FF/RST 38, not NOP.

    // Read OAM at adjacent machine-cycle boundaries around DMA completion.
    for (unsigned finished = 0; finished < 2; ++finished) {
        setup(&m); reset_cpu(&c, &m); c.PC = 0xFF80;
        set_reg(&c, REG_HL, 0xFE00); m.hram[0] = 0x7E; // LD A,(HL), 8 T-cycles.
        memory_write(&m, 0xFF46, 0xC0);
        memory_tick(&m, finished ? 640 : 636);
        CHECK(cpu_step(&c, &m) == 8 && c.A == (finished ? pattern(0) : 0xFF));
        CHECK(m.dma.active == !finished && m.dma.index == (finished ? 160u : 159u));
    }

    setup(&m); reset_cpu(&c, &m); c.PC = 0xFF80;
    const uint8_t accesses[] = {0xFA,0,0xC0,0xEA,2,0xC0,0xE0,0x46};
    memcpy(m.hram, accesses, sizeof(accesses));
    memory_write(&m, 0xFF46, 0xC0); memory_tick(&m, 8);
    CHECK(cpu_step(&c, &m) == 16 && c.A == 0xFF); // HRAM code, blocked WRAM data.
    c.A = 0x55;
    CHECK(cpu_step(&c, &m) == 16 && m.wram[2] == pattern(2));
    c.A = 0xD0;
    CHECK(cpu_step(&c, &m) == 12 && m.dma.start_delay == 8);
    CHECK(m.dma.source == 0xC000 && memory_read(&m, 0xFF46) == 0xD0);
    cpu_step(&c, &m); cpu_step(&c, &m); // HRAM NOPs permit restart to take effect.
    CHECK(m.dma.source == 0xD000 && m.dma.index == 0);

    setup(&m); reset_cpu(&c, &m); c.SP = 0xC100;
    memory_write(&m, 0xC0FE, 0x12); memory_write(&m, 0xC0FF, 0x34);
    memory_write(&m, 0xFF46, 0xC0); memory_tick(&m, 8);
    CHECK(cpu_step(&c, &m) == 16 && c.PC == 0x38 && c.SP == 0xC0FE);
    CHECK(m.wram[0xFE] == 0x12 && m.wram[0xFF] == 0x34); // Stack writes blocked too.

    setup(&m); reset_cpu(&c, &m);
    memory_write(&m, 0xFF40, 0x80); memory_tick(&m, 65664 - 648);
    memory_write(&m, 0xFF04, 0); memory_write(&m, 0xFF05, 0xFF);
    memory_write(&m, 0xFF07, 5); memory_write(&m, 0xFF46, 0xC0);
    c.state = CPU_HALTED;
    for (unsigned i = 0; i < 162; ++i) CHECK(cpu_step(&c, &m) == 4);
    CHECK(!m.dma.active && c.state == CPU_HALTED && c.total_cycles == 648);
    CHECK(m.timer.divider == 648 && m.ppu.ly == 144 && m.ppu.dot == 0);
    CHECK(m.io[0x0F] == 5); // Timer and VBlank requests both survive DMA.
}

#ifdef GB_GRAPHICS_TRACE
static void trace_origin_test(void)
{
    Memory m;
    GraphicsTrace t;
    GraphicsTraceOptions o;
    setup(&m); m.cartridge.rom_loaded = true;
    graphics_trace_options_init(&o); o.enabled = true;
    FILE *out = tmpfile(); CHECK(out);
    CHECK(graphics_trace_start(&t, &m, &o, out));
    memory_write(&m, 0xFF46, 1); memory_tick(&m, 648);
    CHECK(t.cycles == 648);
    CHECK(fflush(out) == 0 && fseek(out, 0, SEEK_SET) == 0);
    char text[1024] = {0}; CHECK(fread(text, 1, sizeof(text) - 1, out) > 0);
    CHECK(!strstr(text, "ROM_DATA_READ")); // DMA must not impersonate CPU reads.
    CHECK(fclose(out) == 0);
}
#endif

int main(void)
{
    progress_tests(); restart_tests(); source_and_batch_tests(); access_tests();
    ppu_access_tests(); cpu_and_clock_tests();
#ifdef GB_GRAPHICS_TRACE
    trace_origin_test();
#endif
    puts("PASS: cycle-progressive DMA, restart, bus restrictions, CPU timing and shared device clock");
    return 0;
}
