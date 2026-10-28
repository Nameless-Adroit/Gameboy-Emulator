#include <stdio.h>
#include <stdlib.h>
#include "cpu.h"

// Unlike assert(), these checks still run in Release / NDEBUG builds.
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static CPU c;
static Memory m;
static void setup(uint8_t op)
{
    memory_init(&m);
    reset_cpu(&c, &m);
    c.F = 0;
    // Seed cartridge bytes directly: CPU bus writes to ROM are ignored.
    m.cartridge.rom_loaded = true;
    m.cartridge.rom[0x100] = op;
}

static void control_tests(void)
{
    setup(0xFB); // EI; NOP: interrupt accepted only after NOP completes.
    m.ie = m.io[0xf] = 5;
    CHECK(cpu_step(&c, &m) == 4 && !c.ime);
    CHECK(cpu_step(&c, &m) == 4 && c.ime && c.PC == 0x102);
    CHECK(cpu_step(&c, &m) == 20 && c.PC == 0x40 && !c.ime);
    CHECK(m.io[0xf] == 4 && c.SP == 0xFFFC);
    CHECK(m.hram[0x7c] == 2 && m.hram[0x7d] == 1);
    m.cartridge.rom[0x40] = 0xD9;
    CHECK(cpu_step(&c, &m) == 16 && c.ime && c.PC == 0x102);
    CHECK(cpu_step(&c, &m) == 20 && c.PC == 0x50 && m.io[0xf] == 0);

    setup(0xF3); c.ime = true;
    CHECK(cpu_step(&c, &m) == 4 && !c.ime);
    m.ie = m.io[0xf] = 1;
    CHECK(cpu_step(&c, &m) == 4 && c.PC == 0x102 && m.io[0xf] == 1);

    setup(0xFB); m.cartridge.rom[0x101] = 0xF3; // DI cancels delayed EI.
    cpu_step(&c, &m); cpu_step(&c, &m); cpu_step(&c, &m);
    CHECK(!c.ime && !c.ime_delay);
    setup(0xFB); m.cartridge.rom[0x101] = 0xFB;
    cpu_step(&c, &m); cpu_step(&c, &m);
    CHECK(c.ime); // Repeated EI cannot postpone the first enable.

    setup(0x76);
    CHECK(cpu_step(&c, &m) == 4 && c.state == CPU_HALTED);
    CHECK(cpu_step(&c, &m) == 4 && c.PC == 0x101);
    m.ie = m.io[0xf] = 1;
    cpu_step(&c, &m);
    CHECK(c.state == CPU_RUNNING && c.PC == 0x102 && m.io[0xf] == 1);

    setup(0x76); c.ime = true; m.io[0xf] = 4; m.ie = 1;
    CHECK(cpu_step(&c, &m) == 4 && c.state == CPU_HALTED);
    CHECK(cpu_step(&c, &m) == 4 && c.state == CPU_HALTED && c.PC == 0x101);

    setup(0x76); m.cartridge.rom[0x101] = 0x3E; m.cartridge.rom[0x102] = 0x99;
    m.ie = m.io[0xf] = 1;
    cpu_step(&c, &m); CHECK(c.halt_bug);
    cpu_step(&c, &m);
    CHECK(c.A == 0x3E && c.PC == 0x102 && !c.halt_bug);

    setup(0xFB); m.cartridge.rom[0x101] = 0x76;
    m.ie = m.io[0xf] = 1;
    cpu_step(&c, &m); cpu_step(&c, &m); cpu_step(&c, &m);
    CHECK(c.PC == 0x40 && m.hram[0x7c] == 1 && m.hram[0x7d] == 1);

    setup(0x10);
    CHECK(cpu_step(&c, &m) == 4 && c.PC == 0x102 && c.state == CPU_STOPPED);
    CHECK(cpu_step(&c, &m) == 0 && c.PC == 0x102);
    cpu_wake_stop(&c); CHECK(c.state == CPU_RUNNING);
    cpu_step(&c, &m); CHECK(c.PC == 0x103);

    const uint8_t illegal[] = {0xD3,0xDB,0xDD,0xE3,0xE4,0xEB,0xEC,0xED,0xF4,0xFC,0xFD};
    for (unsigned i = 0; i < sizeof illegal; ++i) {
        setup(illegal[i]); cpu_step(&c, &m);
        CHECK(c.state == CPU_LOCKED);
        c.ime = true; m.ie = m.io[0xf] = 1;
        cpu_step(&c, &m); CHECK(c.state == CPU_LOCKED && c.PC == 0x101);
    }
    setup(0); c.SP = 0;
    push_word(&c, &m, 0xABCD);
    CHECK(c.SP == 0xFFFE && m.hram[0x7e] == 0xCD && m.ie == 0xAB);
    CHECK(pop_word(&c, &m) == 0xABCD && c.SP == 0);
    setup(0); c.PC = 0xFFFF; m.ie = 0x06; m.cartridge.rom[0] = 0x42;
    CHECK(cpu_step(&c, &m) == 8 && c.B == 0x42 && c.PC == 1);
}

// Published SM83 T-cycle totals; conditional entries use the untaken path.
static void timing_tests(void)
{
    static const uint8_t timing[256] = {
         4,12, 8, 8, 4, 4, 8, 4,20, 8, 8, 8, 4, 4, 8, 4,
         4,12, 8, 8, 4, 4, 8, 4,12, 8, 8, 8, 4, 4, 8, 4,
         8,12, 8, 8, 4, 4, 8, 4, 8, 8, 8, 8, 4, 4, 8, 4,
         8,12, 8, 8,12,12,12, 4, 8, 8, 8, 8, 4, 4, 8, 4,
         4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
         4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
         4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
         8, 8, 8, 8, 8, 8, 4, 8, 4, 4, 4, 4, 4, 4, 8, 4,
         4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
         4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
         4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
         4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
         8,12,12,16,12,16, 8,16, 8,16,12, 8,12,24, 8,16,
         8,12,12, 4,12,16, 8,16, 8,16,12, 4,12, 4, 8,16,
        12,12, 8, 4, 4,16, 8,16,16, 4,16, 4, 4, 4, 8,16,
        12,12, 8, 4, 4,16, 8,16,12, 8,16, 4, 4, 4, 8,16
    };
    const uint8_t branches[4][4] = {
        {0x20,0x28,0x30,0x38}, {0xC2,0xCA,0xD2,0xDA},
        {0xC4,0xCC,0xD4,0xDC}, {0xC0,0xC8,0xD0,0xD8}
    };
    for (unsigned op = 0; op < 256; ++op) {
        setup((uint8_t)op);
        set_reg(&c, REG_HL, 0xC000);
        for (unsigned kind = 0; kind < 4; ++kind)
            for (unsigned cc = 0; cc < 4; ++cc)
                if (op == branches[kind][cc])
                    c.F = (cc & 1) ? 0 : (cc < 2 ? FLAG_Z : FLAG_C);
        CHECK(cpu_step(&c, &m) == timing[op]);
        CHECK(c.cycles == timing[op] && c.total_cycles == timing[op]);
        CHECK(m.timer.divider == timing[op]);
    }
    for (unsigned kind = 0; kind < 4; ++kind)
        for (unsigned cc = 0; cc < 4; ++cc)
            for (unsigned taken = 0; taken < 2; ++taken) {
                setup(branches[kind][cc]);
                bool flag = taken ? (cc & 1) != 0 : (cc & 1) == 0;
                c.F = flag ? (cc < 2 ? FLAG_Z : FLAG_C) : 0;
                m.cartridge.rom[0x101] = 0x34;
                m.cartridge.rom[0x102] = 0x12;
                c.SP = 0xC100;
                memory_write(&m, 0xC100, 0x78);
                memory_write(&m, 0xC101, 0x56);
                const unsigned yes[] = {12,16,24,20}, no[] = {8,12,12,8};
                CHECK(cpu_step(&c, &m) == (taken ? yes[kind] : no[kind]));
                CHECK(m.timer.divider == c.total_cycles);
                CHECK(c.PC == (taken ? (kind == 0 ? 0x136 : kind == 3 ? 0x5678 : 0x1234)
                                                   : (kind == 0 ? 0x102 : kind == 3 ? 0x101 : 0x103)));
            }
    for (unsigned op = 0; op < 256; ++op) {
        setup(0xCB);
        set_reg(&c, REG_HL, 0xC000);
        m.cartridge.rom[0x101] = (uint8_t)op;
        unsigned expected = (op & 7) != 6 ? 8 : (op >= 0x40 && op < 0x80 ? 12 : 16);
        CHECK(cpu_step(&c, &m) == expected);
        CHECK(m.timer.divider == expected && c.total_cycles == expected);
    }
}

static void bus_timing_tests(void)
{
    setup(0);
    memory_write(&m, 0xC000, 0x23);
    CHECK(memory_read(&m, 0xC000) == 0x23);
    CHECK(memory_read(&m, 0xFF04) == 0);
    CHECK(m.timer.divider == 0 && c.total_cycles == 0);

    // LDH A,(DIV) samples on its third machine cycle, not at decode time.
    setup(0xF0); m.cartridge.rom[0x101] = 4;
    m.timer.divider = 244;
    CHECK(cpu_step(&c, &m) == 12 && c.A == 1);
    CHECK(m.timer.divider == 256 && c.total_cycles == 12);

    // LDH (DIV),A resets at the write cycle, with no extra tick afterwards.
    setup(0xE0); m.cartridge.rom[0x101] = 4;
    CHECK(cpu_step(&c, &m) == 12 && m.timer.divider == 0);
    CHECK(c.total_cycles == 12);

    // INC (TIMA): timer edge between read and write must not change the
    // previously sampled operand. The write replaces TIMA with 0x11.
    setup(0x34); set_reg(&c, REG_HL, 0xFF05);
    m.timer.divider = 4;
    memory_write(&m, 0xFF05, 0x10);
    memory_write(&m, 0xFF07, 5);
    CHECK(cpu_step(&c, &m) == 12 && m.timer.tima == 0x11);
    CHECK(m.timer.divider == 16);

    // PUSH's internal cycle precedes stack writes: high byte resets DIV
    // on cycle three, leaving exactly one machine cycle before completion.
    setup(0xC5); c.SP = 0xFF05; set_reg(&c, REG_BC, 0x1234);
    CHECK(cpu_step(&c, &m) == 16 && m.timer.divider == 4);

    // Taken RET cc spends an internal cycle before reading the stack.
    setup(0xC0); c.SP = 0xFF05;
    m.timer.divider = 4;
    memory_write(&m, 0xFF06, 0x42);
    memory_write(&m, 0xFF07, 5);
    CHECK(cpu_step(&c, &m) == 20 && c.PC == 0x4201);
    CHECK(m.timer.divider == 24);

    setup(0x10);
    CHECK(cpu_step(&c, &m) == 4 && m.timer.divider == 4);
    CHECK(cpu_step(&c, &m) == 0 && m.timer.divider == 4);
    CHECK(c.cycles == 0 && c.total_cycles == 4);
}

static void timer_interrupt_tests(void)
{
    // Every interrupt vector, masking and priority, preserving unrelated IF bits.
    for (unsigned bit = 0; bit < 5; ++bit) {
        setup(0);
        c.ime = true;
        m.ie = (uint8_t)(0x1F << bit);
        m.io[0x0F] = 0xFF;
        CHECK(cpu_step(&c, &m) == 20);
        CHECK(c.PC == 0x40 + 8 * bit && !c.ime);
        CHECK(m.io[0x0F] == (uint8_t)(0xFF & ~(1u << bit)));
        CHECK(pop_word(&c, &m) == 0x100);
    }
    setup(0); c.ime = true; m.ie = 0xE0; m.io[0x0F] = 0xFF;
    CHECK(cpu_step(&c, &m) == 4 && c.PC == 0x101 && c.ime);

    // End-to-end: timer advances during HALT, requests IRQ, dispatches, RETI resumes.
    setup(0x76); c.ime = true; m.ie = 4;
    m.cartridge.rom[0x50] = 0xD9;
    memory_write(&m, 0xFF05, 0xFF);
    memory_write(&m, 0xFF06, 0x80);
    memory_write(&m, 0xFF07, 5);
    for (unsigned i = 0; i < 5; ++i) {
        cpu_step(&c, &m);
        CHECK(c.state == CPU_HALTED && c.PC == 0x101);
    }
    CHECK(memory_read(&m, 0xFF0F) == 4);
    cpu_step(&c, &m);
    CHECK(c.state == CPU_RUNNING && c.PC == 0x50 && !c.ime);
    CHECK(memory_read(&m, 0xFF0F) == 0);
    cpu_step(&c, &m);
    CHECK(c.PC == 0x101 && c.ime && c.SP == 0xFFFE);
    CHECK(c.total_cycles == 56 && m.timer.divider == 56);
}

// Exhaust all ADD/ADC/SUB/SBC/CP operands and incoming carry values.
// Expected flags use carry/borrow identities, independent of decoder helpers.
static void arithmetic_tests(void)
{
    const uint8_t ops[] = {0xC6,0xCE,0xD6,0xDE,0xFE};
    setup(0);
    for (unsigned op = 0; op < sizeof ops; ++op)
        for (unsigned a = 0; a < 256; ++a)
            for (unsigned b = 0; b < 256; ++b)
                for (unsigned carry = 0; carry < 2; ++carry) {
                    c.PC = 0x100; c.A = (uint8_t)a; c.F = carry ? FLAG_C : 0;
                    m.cartridge.rom[0x100] = ops[op]; m.cartridge.rom[0x101] = (uint8_t)b;
                    unsigned ci = (op == 1 || op == 3) ? carry : 0;
                    int wide = op < 2 ? (int)(a + b + ci) : (int)a - (int)b - (int)ci;
                    uint8_t result = (uint8_t)wide;
                    uint8_t flags = (result == 0 ? FLAG_Z : 0)
                        | (op >= 2 ? FLAG_N : 0)
                        | (((a ^ b ^ (unsigned)wide) & 16) ? FLAG_H : 0)
                        | ((wide < 0 || wide > 255) ? FLAG_C : 0);
                    CHECK(cpu_step(&c, &m) == 8);
                    CHECK(c.A == (op == 4 ? a : result) && c.F == flags);
                }
}

// Decimal arithmetic supplies an independent oracle for valid BCD inputs.
static uint8_t bcd(unsigned n) { return (uint8_t)(((n / 10) << 4) | (n % 10)); }
static void decimal_tests(void)
{
    setup(0);
    for (unsigned a = 0; a < 100; ++a)
        for (unsigned b = 0; b < 100; ++b)
            for (unsigned sub = 0; sub < 2; ++sub)
                for (unsigned carry = 0; carry < 2; ++carry) {
                    c.PC = 0x100; c.A = bcd(a); c.F = carry ? FLAG_C : 0;
                    m.cartridge.rom[0x100] = sub ? 0xDE : 0xCE;
                    m.cartridge.rom[0x101] = bcd(b); m.cartridge.rom[0x102] = 0x27;
                    cpu_step(&c, &m); cpu_step(&c, &m);
                    int expected = sub ? (int)a - (int)b - (int)carry : (int)(a + b + carry);
                    bool cy = expected < 0 || expected >= 100;
                    expected = (expected + 100) % 100;
                    CHECK(c.A == bcd((unsigned)expected));
                    CHECK(c.F == ((sub ? FLAG_N : 0) | (cy ? FLAG_C : 0)
                                  | (expected == 0 ? FLAG_Z : 0)));
                }
}
int main(void)
{
    control_tests(); timing_tests(); bus_timing_tests(); timer_interrupt_tests();
    arithmetic_tests(); decimal_tests();
    puts("PASS: opcode timing, timer interrupts, control boundaries, 655360 ALU cases, 40000 BCD cases");
    return 0;
}
