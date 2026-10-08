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
    m.data[0x100] = op;
}

static void control_tests(void)
{
    setup(0xFB); // EI; NOP: interrupt accepted only after NOP completes.
    m.data[0xFFFF] = m.data[0xFF0F] = 5;
    CHECK(cpu_step(&c, &m) == 4 && !c.ime);
    CHECK(cpu_step(&c, &m) == 4 && c.ime && c.PC == 0x102);
    CHECK(cpu_step(&c, &m) == 20 && c.PC == 0x40 && !c.ime);
    CHECK(m.data[0xFF0F] == 4 && c.SP == 0xFFFC);
    CHECK(m.data[0xFFFC] == 2 && m.data[0xFFFD] == 1);
    m.data[0x40] = 0xD9;
    CHECK(cpu_step(&c, &m) == 16 && c.ime && c.PC == 0x102);

    setup(0xFB); m.data[0x101] = 0xF3; // DI cancels delayed EI.
    cpu_step(&c, &m); cpu_step(&c, &m); cpu_step(&c, &m);
    CHECK(!c.ime && !c.ime_delay);
    setup(0xFB); m.data[0x101] = 0xFB;
    cpu_step(&c, &m); cpu_step(&c, &m);
    CHECK(c.ime); // Repeated EI cannot postpone the first enable.

    setup(0x76);
    CHECK(cpu_step(&c, &m) == 4 && c.state == CPU_HALTED);
    CHECK(cpu_step(&c, &m) == 4 && c.PC == 0x101);
    m.data[0xFFFF] = m.data[0xFF0F] = 1;
    cpu_step(&c, &m);
    CHECK(c.state == CPU_RUNNING && c.PC == 0x102 && m.data[0xFF0F] == 1);

    setup(0x76); m.data[0x101] = 0x3E; m.data[0x102] = 0x99;
    m.data[0xFFFF] = m.data[0xFF0F] = 1;
    cpu_step(&c, &m); CHECK(c.halt_bug);
    cpu_step(&c, &m);
    CHECK(c.A == 0x3E && c.PC == 0x102 && !c.halt_bug);

    setup(0xFB); m.data[0x101] = 0x76;
    m.data[0xFFFF] = m.data[0xFF0F] = 1;
    cpu_step(&c, &m); cpu_step(&c, &m); cpu_step(&c, &m);
    CHECK(c.PC == 0x40 && m.data[0xFFFC] == 1 && m.data[0xFFFD] == 1);

    setup(0x10);
    CHECK(cpu_step(&c, &m) == 4 && c.PC == 0x102 && c.state == CPU_STOPPED);
    CHECK(cpu_step(&c, &m) == 0 && c.PC == 0x102);
    cpu_wake_stop(&c); CHECK(c.state == CPU_RUNNING);
    cpu_step(&c, &m); CHECK(c.PC == 0x103);

    const uint8_t illegal[] = {0xD3,0xDB,0xDD,0xE3,0xE4,0xEB,0xEC,0xED,0xF4,0xFC,0xFD};
    for (unsigned i = 0; i < sizeof illegal; ++i) {
        setup(illegal[i]); cpu_step(&c, &m);
        CHECK(c.state == CPU_LOCKED);
        c.ime = true; m.data[0xFFFF] = m.data[0xFF0F] = 1;
        cpu_step(&c, &m); CHECK(c.state == CPU_LOCKED && c.PC == 0x101);
    }
    setup(0); c.SP = 0;
    push_word(&c, &m, 0xABCD);
    CHECK(c.SP == 0xFFFE && m.data[0xFFFE] == 0xCD && m.data[0xFFFF] == 0xAB);
    CHECK(pop_word(&c, &m) == 0xABCD && c.SP == 0);
    setup(0); c.PC = 0xFFFF; m.data[0xFFFF] = 0x06; m.data[0] = 0x42;
    CHECK(cpu_step(&c, &m) == 8 && c.B == 0x42 && c.PC == 1);
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
                    m.data[0x100] = ops[op]; m.data[0x101] = (uint8_t)b;
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
                    m.data[0x100] = sub ? 0xDE : 0xCE;
                    m.data[0x101] = bcd(b); m.data[0x102] = 0x27;
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
    control_tests(); arithmetic_tests(); decimal_tests();
    puts("PASS: control/interrupt boundaries, 655360 ALU cases, 40000 BCD cases");
    return 0;
}
