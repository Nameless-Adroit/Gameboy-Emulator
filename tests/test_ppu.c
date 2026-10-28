#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ppu.h"
#include "ppu.h" // The public header must be safe to include twice.
#include "cpu.h"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static unsigned mode(PPU *p) { return ppu_read(p, 0xFF41) & 3u; }

static void register_tests(void)
{
    PPU p;
    uint8_t flags = 0;
    ppu_init(&p);
    CHECK(p.dot == 0 && p.ly == 0 && !p.frame_ready);
    CHECK(ppu_read(&p, 0xFF41) == 0x84);
    const uint16_t regs[] = {0xFF42,0xFF43,0xFF45,0xFF47,0xFF48,0xFF49,0xFF4A,0xFF4B};
    for (unsigned i = 0; i < sizeof(regs) / sizeof(regs[0]); ++i) {
        ppu_write(&p, regs[i], (uint8_t)(0x20 + i));
        CHECK(ppu_read(&p, regs[i]) == 0x20 + i);
    }
    ppu_write(&p, 0xFF44, 99);
    CHECK(ppu_read(&p, 0xFF44) == 0);
    ppu_write(&p, 0xFF41, 0xFF);
    CHECK(ppu_read(&p, 0xFF41) == 0xF8); // Status bits cannot be written.
    ppu_tick(&p, 70224, &flags);
    CHECK(p.dot == 0 && p.ly == 0 && flags == 0); // LCD off freezes PPU.
    CHECK(ppu_read(&p, 0xFF46) == 0xFF);
    ppu_write(&p, 0xFF46, 0xFF);
    CHECK(p.lcdc == 0);
}

static void frame_tests(void)
{
    PPU p;
    uint8_t flags = 0x10;
    ppu_init(&p);
    ppu_write(&p, 0xFF40, 0x80);
    CHECK(mode(&p) == 2);
    ppu_tick(&p, 79, &flags); CHECK(mode(&p) == 2 && p.dot == 79);
    ppu_tick(&p, 1, &flags); CHECK(mode(&p) == 3);
    ppu_tick(&p, 171, &flags); CHECK(mode(&p) == 3);
    ppu_tick(&p, 1, &flags); CHECK(mode(&p) == 0);
    ppu_tick(&p, 203, &flags); CHECK(p.ly == 0 && p.dot == 455);
    ppu_tick(&p, 1, &flags); CHECK(p.ly == 1 && p.dot == 0 && mode(&p) == 2);
    ppu_tick(&p, 456 * 143 - 1, &flags);
    CHECK(p.ly == 143 && !p.frame_ready && flags == 0x10);
    ppu_tick(&p, 1, &flags);
    CHECK(p.ly == 144 && p.dot == 0 && mode(&p) == 1);
    CHECK(p.frame_ready && flags == 0x11);
    p.frame_ready = false;
    flags = 0;
    ppu_tick(&p, 456 * 10, &flags);
    CHECK(p.ly == 0 && p.dot == 0 && mode(&p) == 2);
    CHECK(!p.frame_ready && flags == 0);
    ppu_tick(&p, 456 * 144, &flags);
    CHECK(p.frame_ready && flags == 1);
    ppu_write(&p, 0xFF40, 0);
    CHECK(p.ly == 0 && p.dot == 0 && mode(&p) == 0 && !p.frame_ready);
    ppu_write(&p, 0xFF40, 0x80);
    CHECK(p.ly == 0 && p.dot == 0 && mode(&p) == 2);
}

static void stat_tests(void)
{
    PPU p;
    uint8_t flags = 0x10;
    ppu_init(&p);
    ppu_write(&p, 0xFF40, 0x80);
    ppu_write(&p, 0xFF41, 0x20); // Enabling the active OAM source raises IRQ.
    ppu_tick(&p, 0, &flags); CHECK(flags == 0x12 && p.dot == 0);
    flags = 0;
    ppu_tick(&p, 79, &flags); CHECK(flags == 0);
    ppu_tick(&p, 377, &flags); CHECK(flags == 2 && p.ly == 1);

    // Overlapping HBlank and OAM sources remain high across line wrap.
    ppu_write(&p, 0xFF41, 0x28);
    flags = 0;
    ppu_tick(&p, 252, &flags); CHECK(flags == 2 && mode(&p) == 0);
    flags = 0;
    ppu_tick(&p, 204, &flags); CHECK(flags == 0 && mode(&p) == 2);

    // Coincidence updates on writes and shares the same edge detection.
    ppu_write(&p, 0xFF41, 0x40);
    ppu_tick(&p, 0, &flags); flags = 0;
    ppu_write(&p, 0xFF45, p.ly);
    ppu_tick(&p, 0, &flags);
    CHECK(flags == 2 && (ppu_read(&p, 0xFF41) & 4));
    flags = 0;
    ppu_write(&p, 0xFF45, p.ly);
    ppu_tick(&p, 0, &flags); CHECK(flags == 0);
    ppu_tick(&p, 456, &flags);
    CHECK(!(ppu_read(&p, 0xFF41) & 4));

    ppu_init(&p); flags = 0x10;
    ppu_write(&p, 0xFF41, 0x10);
    ppu_write(&p, 0xFF40, 0x80);
    ppu_tick(&p, 456 * 144, &flags);
    CHECK(flags == 0x13); // VBlank and STAT preserve unrelated IF bits.
}

static void batching_tests(void)
{
    PPU a, b;
    uint8_t fa = 0, fb = 0;
    ppu_init(&a); ppu_init(&b);
    ppu_write(&a, 0xFF40, 0x80); ppu_write(&b, 0xFF40, 0x80);
    ppu_write(&a, 0xFF41, 0x78); ppu_write(&b, 0xFF41, 0x78);
    ppu_tick(&a, 70224 * 2 + 300, &fa);
    for (unsigned i = 0; i < 70224 * 2 + 300; ++i) ppu_tick(&b, 1, &fb);
    CHECK(a.dot == b.dot && a.ly == b.ly && a.stat == b.stat);
    CHECK(a.stat_irq_line == b.stat_irq_line && fa == fb);
    CHECK(a.frame_ready == b.frame_ready);
    CHECK(memcmp(a.framebuffer, b.framebuffer, sizeof(a.framebuffer)) == 0);
}

/* Independent frame schedule: each visible line has boundaries at dots
 * 0 (OAM), 80 (transfer), 252 (HBlank); VBlank starts at frame dot 65664.
 * Check every dot, all STAT source masks and representative LYC values over
 * two frames. Clear IF after each sample to expose missing/duplicate edges. */
static void exhaustive_frame_tests(void)
{
    const uint8_t comparisons[] = {0, 87, 144, 153};
    for (unsigned mask = 0; mask < 16; ++mask) {
        for (unsigned ci = 0; ci < sizeof(comparisons); ++ci) {
            PPU p;
            uint8_t flags = 0;
            bool previous_line = false;
            ppu_init(&p);
            ppu_write(&p, 0xFF45, comparisons[ci]);
            ppu_write(&p, 0xFF41, (uint8_t)(mask << 3));
            ppu_write(&p, 0xFF40, 0x80);
            for (unsigned elapsed = 0; elapsed <= 2 * 70224; ++elapsed) {
                unsigned frame_dot = elapsed % 70224;
                unsigned line = frame_dot / 456;
                unsigned dot = frame_dot % 456;
                unsigned expected_mode = 1;
                if (line < 144) {
                    expected_mode = dot < 80 ? 2u : dot < 252 ? 3u : 0u;
                }
                bool equal = line == comparisons[ci];
                unsigned sources = (equal ? 8u : 0u);
                switch (expected_mode) {
                case 0: sources |= 1; break;
                case 1: sources |= 2; break;
                case 2: sources |= 4; break;
                default: break;
                }
                bool irq_line = (mask & sources) != 0;
                uint8_t expected_flags = 0x10;
                if (irq_line && !previous_line) expected_flags |= 2;
                if (frame_dot == 65664) expected_flags |= 1;
                flags = 0x10;
                ppu_tick(&p, elapsed == 0 ? 0 : 1, &flags);
                CHECK(p.ly == line && p.dot == dot && mode(&p) == expected_mode);
                CHECK(((ppu_read(&p, 0xFF41) & 4) != 0) == equal);
                CHECK(flags == expected_flags);
                if (frame_dot == 65664) {
                    CHECK(p.frame_ready);
                    p.frame_ready = false;
                } else {
                    CHECK(!p.frame_ready);
                }
                previous_line = irq_line;
            }
        }
    }
}

static void lcd_and_write_interrupt_tests(void)
{
    Memory m;
    memory_init(&m);
    memory_write(&m, 0xFF41, 0x78);
    memory_write(&m, 0xFF45, 0);
    memory_tick(&m, 70224);
    CHECK(m.io[0x0F] == 0 && m.ppu.dot == 0 && m.ppu.ly == 0);
    memory_write(&m, 0xFF40, 0x80);
    CHECK(m.io[0x0F] == 2); // Enable active OAM/coincidence sources.
    memory_write(&m, 0xFF0F, 0);
    memory_write(&m, 0xFF41, 0x78);
    memory_tick(&m, 1);
    CHECK(m.io[0x0F] == 0); // Clearing IF cannot create a new STAT edge.
    memory_write(&m, 0xFF40, 0);
    CHECK(!m.ppu.stat_irq_line && m.ppu.ly == 0 && m.ppu.dot == 0);
    memory_write(&m, 0xFF40, 0x80);
    CHECK(m.io[0x0F] == 2); // LCD disable released the combined line.

    memory_write(&m, 0xFF41, 0);
    memory_tick(&m, 100); // Mode 3 has no dedicated STAT source.
    memory_write(&m, 0xFF0F, 0x11);
    memory_write(&m, 0xFF41, 0x38);
    CHECK(m.io[0x0F] == 0x11);
    memory_write(&m, 0xFF41, 0x40); // LYC=0 currently matches LY=0.
    CHECK(m.io[0x0F] == 0x13);
    memory_write(&m, 0xFF0F, 0);
    memory_write(&m, 0xFF45, 1); // Lower the source, then raise it again.
    CHECK(m.io[0x0F] == 0);
    memory_write(&m, 0xFF45, 0);
    CHECK(m.io[0x0F] == 2 && m.ppu.dot == 100);
    memory_write(&m, 0xFF44, 123);
    CHECK(m.ppu.ly == 0 && m.ppu.dot == 100);
    memory_write(&m, 0xFF40, 0x81); // Keeping LCD enabled must not restart it.
    CHECK(m.ppu.dot == 100);
    memory_write(&m, 0xFF40, 0);
    CHECK(m.ppu.dot == 0 && !m.ppu.frame_ready);
    CHECK(m.io[0x0F] == 2); // Disabling LCD must not erase an existing request.
}

static void bus_and_cpu_tests(void)
{
    Memory m;
    CPU c;
    memory_init(&m); reset_cpu(&c, &m);
    memory_write(&m, 0xFF40, 0x80);
    memory_write(&m, 0xFF41, 0x20);
    CHECK(memory_read(&m, 0xFF0F) == 2); // Write-triggered IRQ is immediate.
    CHECK(m.ppu.dot == 0 && m.timer.divider == 0 && c.total_cycles == 0);
    CHECK(memory_read(&m, 0xFF41) == 0xA6 && m.ppu.dot == 0);
    memory_write(&m, 0xFF46, 0xC0);
    CHECK(memory_read(&m, 0xFF46) == 0xC0 && m.ppu.lcdc == 0x80);

    memory_write(&m, 0xFF41, 0);
    memory_write(&m, 0xFF0F, 0);
    memory_tick(&m, 456 * 144 - 4);
    c.state = CPU_HALTED; c.ime = true; m.ie = 1;
    CHECK(cpu_step(&c, &m) == 4 && c.state == CPU_HALTED);
    CHECK(m.ppu.ly == 144 && m.ppu.dot == 0 && m.io[0x0F] == 1);
    CHECK(cpu_step(&c, &m) == 20 && c.PC == 0x40 && c.state == CPU_RUNNING);
    CHECK(m.ppu.dot == 20 && c.total_cycles == 24);
    CHECK(m.timer.divider == (uint16_t)(456 * 144 + 20));
    CHECK(m.io[0x0F] == 0);
}

static void interrupt_delivery_tests(void)
{
    Memory m;
    CPU c;
    memory_init(&m); reset_cpu(&c, &m);
    memory_write(&m, 0xFF40, 0x80);
    memory_write(&m, 0xFF41, 0x10);
    memory_tick(&m, 65664 - 4);
    c.state = CPU_HALTED; c.ime = true; m.ie = 3;
    m.cartridge.rom_loaded = true;
    m.cartridge.rom[0x40] = 0xD9; // RETI permits the second pending interrupt.
    CHECK(cpu_step(&c, &m) == 4 && m.io[0x0F] == 3);
    CHECK(cpu_step(&c, &m) == 20 && c.PC == 0x40 && m.io[0x0F] == 2);
    CHECK(cpu_step(&c, &m) == 16 && c.ime);
    CHECK(cpu_step(&c, &m) == 20 && c.PC == 0x48 && m.io[0x0F] == 0);
    CHECK(m.ppu.dot == 56 && c.total_cycles == 60);

    // With IME clear, an enabled STAT request wakes HALT without dispatch.
    memory_init(&m); reset_cpu(&c, &m);
    memory_write(&m, 0xFF40, 0x80);
    memory_write(&m, 0xFF41, 0x08);
    memory_tick(&m, 248);
    c.state = CPU_HALTED; m.ie = 2;
    m.cartridge.rom_loaded = true; // NOP at entry.
    CHECK(cpu_step(&c, &m) == 4 && m.io[0x0F] == 2);
    CHECK(cpu_step(&c, &m) == 4 && c.state == CPU_RUNNING && c.PC == 0x101);
    CHECK(!c.ime && m.io[0x0F] == 2 && m.ppu.dot == 256);

    // The same clock batch can request timer, STAT and VBlank together.
    memory_init(&m);
    memory_write(&m, 0xFF40, 0x80);
    memory_write(&m, 0xFF41, 0x10);
    memory_tick(&m, 65664 - 20);
    memory_write(&m, 0xFF04, 0);
    memory_write(&m, 0xFF05, 0xFF);
    memory_write(&m, 0xFF07, 5);
    memory_write(&m, 0xFF0F, 0x10);
    memory_tick(&m, 20);
    CHECK(m.io[0x0F] == 0x17 && m.ppu.ly == 144 && m.ppu.dot == 0);
    CHECK(m.timer.divider == 20);
}

static void cpu_register_access_tests(void)
{
    Memory m;
    CPU c;
    memory_init(&m); reset_cpu(&c, &m);
    m.cartridge.rom_loaded = true;
    // LDH (SCX),A; LDH A,(LY); LDH (LYC),A.
    const uint8_t program[] = {0xE0,0x43,0xF0,0x44,0xE0,0x45};
    memcpy(&m.cartridge.rom[0x100], program, sizeof(program));
    memory_write(&m, 0xFF40, 0x80);
    c.A = 0x39;
    CHECK(cpu_step(&c, &m) == 12);
    CHECK(memory_read(&m, 0xFF43) == 0x39 && m.ppu.dot == 12);
    CHECK(m.timer.divider == 12 && c.total_cycles == 12);

    memory_tick(&m, 440); // Reach dot 452, four dots before the next line.
    CHECK(cpu_step(&c, &m) == 12 && c.A == 1);
    CHECK(m.ppu.ly == 1 && m.ppu.dot == 8 && m.timer.divider == 464);
    CHECK(c.total_cycles == 24); // External ticks do not alter CPU counters.

    memory_write(&m, 0xFF41, 0x40); // Enable coincidence, currently false.
    CHECK(m.io[0x0F] == 0);
    CHECK(cpu_step(&c, &m) == 12);
    CHECK(m.ppu.lyc == 1 && m.io[0x0F] == 2);
    CHECK(m.ppu.dot == 20 && m.timer.divider == 476);
    CHECK(c.total_cycles == 36); // No second tick for the register write.
}

int main(void)
{
    register_tests(); frame_tests(); stat_tests(); batching_tests();
    exhaustive_frame_tests(); lcd_and_write_interrupt_tests(); bus_and_cpu_tests();
    interrupt_delivery_tests(); cpu_register_access_tests();
    puts("PASS: PPU registers, modes, frames, STAT edges, batching and shared CPU clock");
    return 0;
}
