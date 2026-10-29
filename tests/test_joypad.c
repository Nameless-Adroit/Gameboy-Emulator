#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu.h"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static uint8_t expected(unsigned select, unsigned pressed)
{
    unsigned value = 0xCF | select;
    for (unsigned line = 0; line < 4; ++line) {
        bool down = (!(select & 0x10) && (pressed & (1u << line))) ||
                    (!(select & 0x20) && (pressed & (16u << line)));
        if (down) value &= ~(1u << line);
    }
    return (uint8_t)value;
}

static void matrix_tests(void)
{
    Joypad j;
    uint8_t flags = 0;
    joypad_init(&j); CHECK(joypad_read(&j) == 0xFF && j.pressed == 0);
    for (unsigned value = 0; value < 256; ++value) {
        joypad_write(&j, (uint8_t)value, &flags);
        CHECK(joypad_read(&j) == (uint8_t)(0xCF | (value & 0x30)));
    }
    CHECK(flags == 0);
    // All physical combinations and all pairwise transitions under each select.
    for (unsigned select = 0; select <= 0x30; select += 0x10)
        for (unsigned before = 0; before < 256; ++before) {
            joypad_init(&j); joypad_write(&j, (uint8_t)select, &flags);
            joypad_set_buttons(&j, (uint8_t)before, &flags);
            CHECK(joypad_read(&j) == expected(select, before));
            for (unsigned after = 0; after < 256; ++after) {
                joypad_set_buttons(&j, (uint8_t)before, &flags);
                flags = 0xA5; j.wake_pending = false;
                joypad_set_buttons(&j, (uint8_t)after, &flags);
                bool edge = (expected(select, before) & (uint8_t)~expected(select, after) & 15) != 0;
                CHECK(joypad_read(&j) == expected(select, after));
                CHECK(flags == (edge ? 0xB5 : 0xA5) && j.wake_pending == edge);
            }
        }
}

static void selection_edge_tests(void)
{
    Joypad j;
    uint8_t flags = 0;
    joypad_init(&j);
    joypad_set_button(&j, JOYPAD_A, true, &flags);
    CHECK(flags == 0 && !j.wake_pending && joypad_read(&j) == 0xFF);
    joypad_write(&j, 0x10, &flags); // Select previously held action button.
    CHECK(flags == 0x10 && j.wake_pending && joypad_read(&j) == 0xDE);
    flags = 0; j.wake_pending = false;
    joypad_write(&j, 0xDF, &flags); // Only bits 4/5 writable; same selection.
    CHECK(flags == 0 && joypad_read(&j) == 0xDE);
    joypad_write(&j, 0, &flags); // Both rows, A already holds line zero low.
    joypad_set_button(&j, JOYPAD_RIGHT, true, &flags);
    CHECK(flags == 0 && joypad_read(&j) == 0xCE);
    joypad_set_button(&j, JOYPAD_A, false, &flags);
    CHECK(flags == 0 && joypad_read(&j) == 0xCE);
    joypad_set_button(&j, JOYPAD_RIGHT, false, &flags);
    CHECK(flags == 0 && joypad_read(&j) == 0xCF);
    joypad_set_button(&j, JOYPAD_RIGHT, true, &flags);
    CHECK(flags == 0x10);
    flags = 0; j.wake_pending = false;
    joypad_set_button(&j, JOYPAD_RIGHT, true, &flags);
    CHECK(flags == 0 && !j.wake_pending); // Held/autorepeated button is not a new edge.
}

static void bus_and_cpu_tests(void)
{
    Memory m;
    CPU c;
    memory_init(&m); reset_cpu(&c, &m);
    CHECK(memory_read(&m, 0xFF00) == 0xFF);
    memory_write(&m, 0xFF00, 0x20);
    joypad_set_button(&m.joypad, JOYPAD_RIGHT, true, &m.io[0x0F]);
    CHECK(memory_cpu_read(&m, 0xFF00) == 0xEE && m.io[0x0F] == 0x10);
    CHECK(m.timer.divider == 0 && m.ppu.dot == 0 && c.total_cycles == 0);
    m.cartridge.rom_loaded = true;
    m.cartridge.rom[0x100] = 0xF0; m.cartridge.rom[0x101] = 0;
    CHECK(cpu_step(&c, &m) == 12 && c.A == 0xEE && c.total_cycles == 12);
    m.cartridge.rom[0x102] = 0xE0; m.cartridge.rom[0x103] = 0;
    joypad_set_button(&m.joypad, JOYPAD_A, true, &m.io[0x0F]);
    joypad_set_button(&m.joypad, JOYPAD_RIGHT, false, &m.io[0x0F]);
    m.io[0x0F] = 0; c.A = 0x10;
    CHECK(cpu_step(&c, &m) == 12 && m.joypad.select == 0x10);
    CHECK(m.io[0x0F] == 0x10 && m.timer.divider == 24);

    memory_init(&m);
    memory_write(&m, 0xFF00, 0x20);
    memory_write(&m, 0xFF46, 0xC0); memory_tick(&m, 8);
    CHECK(memory_cpu_read(&m, 0xFF00) == 0xFF);
    memory_cpu_write(&m, 0xFF00, 0x10); CHECK(m.joypad.select == 0x20);
    joypad_set_button(&m.joypad, JOYPAD_RIGHT, true, &m.io[0x0F]);
    CHECK(m.io[0x0F] == 0x10 && memory_read(&m, 0xFF00) == 0xEE);
    CHECK(m.timer.divider == 8 && m.dma.index == 0); // External device input bypasses CPU grants, not time.
}

static void interrupt_and_stop_tests(void)
{
    Memory m;
    CPU c;
    for (unsigned ime = 0; ime < 2; ++ime) {
        memory_init(&m); reset_cpu(&c, &m);
        m.cartridge.rom_loaded = true; m.cartridge.rom[0x100] = 0x76;
        memory_write(&m, 0xFF00, 0x20); m.ie = 0x10; c.ime = ime != 0;
        CHECK(cpu_step(&c, &m) == 4 && c.state == CPU_HALTED);
        m.io[0x0F] = 0x80;
        joypad_set_button(&m.joypad, JOYPAD_RIGHT, true, &m.io[0x0F]);
        CHECK(m.io[0x0F] == 0x90);
        CHECK(cpu_step(&c, &m) == (ime ? 20u : 4u));
        CHECK(c.state == CPU_RUNNING && c.PC == (ime ? 0x60 : 0x102));
        CHECK(m.io[0x0F] == (ime ? 0x80 : 0x90));
        CHECK(c.total_cycles == m.timer.divider);
    }
    memory_init(&m); reset_cpu(&c, &m);
    m.cartridge.rom_loaded = true; m.cartridge.rom[0x100] = 0x10;
    memory_write(&m, 0xFF00, 0x20);
    CHECK(cpu_step(&c, &m) == 4 && c.state == CPU_STOPPED);
    joypad_set_button(&m.joypad, JOYPAD_A, true, &m.io[0x0F]);
    CHECK(cpu_step(&c, &m) == 0 && c.state == CPU_STOPPED);
    joypad_set_button(&m.joypad, JOYPAD_RIGHT, true, &m.io[0x0F]);
    CHECK(cpu_step(&c, &m) == 4 && c.state == CPU_RUNNING && c.PC == 0x103);
    CHECK(c.total_cycles == 8 && m.timer.divider == 8 && m.ie == 0); // Wake independent of IE/IME.

    memory_init(&m); reset_cpu(&c, &m);
    m.cartridge.rom_loaded = true; m.cartridge.rom[0x100] = 0x10;
    memory_write(&m, 0xFF00, 0x20);
    joypad_set_button(&m.joypad, JOYPAD_RIGHT, true, &m.io[0x0F]);
    CHECK(cpu_step(&c, &m) == 4 && c.state == CPU_STOPPED);
    joypad_set_button(&m.joypad, JOYPAD_RIGHT, true, &m.io[0x0F]);
    CHECK(cpu_step(&c, &m) == 0); // A pre-STOP held level is not a fresh wake event.
    joypad_set_button(&m.joypad, JOYPAD_RIGHT, false, &m.io[0x0F]);
    CHECK(cpu_step(&c, &m) == 0);
    joypad_set_button(&m.joypad, JOYPAD_RIGHT, true, &m.io[0x0F]);
    CHECK(cpu_step(&c, &m) == 4 && c.state == CPU_RUNNING);
}

int main(void)
{
    matrix_tests(); selection_edge_tests(); bus_and_cpu_tests(); interrupt_and_stop_tests();
    puts("PASS: FF00 matrix, all input transitions, IRQ edges, CPU bus, DMA, HALT and STOP wake");
    return 0;
}
