#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static void transfer_and_control(void)
{
    Memory m; memory_init(&m);
    CHECK(memory_read(&m, 0xFF02) == 0x7E);
    memory_write(&m, 0xFF0F, 0x11);
    memory_write(&m, 0xFF01, 0);
    memory_write(&m, 0xFF02, 0x83); /* DMG ignores CGB fast-clock bit. */
    CHECK(memory_read(&m, 0xFF02) == 0xFF);
    memory_tick(&m, 511); CHECK(memory_read(&m, 0xFF01) == 0);
    memory_tick(&m, 1); CHECK(memory_read(&m, 0xFF01) == 1);
    memory_tick(&m, 512 * 6); CHECK(memory_read(&m, 0xFF01) == 0x7F);
    CHECK(memory_read(&m, 0xFF0F) == 0x11);
    memory_tick(&m, 511); CHECK(memory_read(&m, 0xFF02) & 0x80);
    memory_tick(&m, 1);
    CHECK(memory_read(&m, 0xFF01) == 0xFF);
    CHECK(memory_read(&m, 0xFF02) == 0x7F);
    CHECK(memory_read(&m, 0xFF0F) == 0x19);
    memory_write(&m, 0xFF0F, 0);
    memory_tick(&m, 8192); CHECK(memory_read(&m, 0xFF0F) == 0);
    /* An external-clock request cannot complete without a cable clock. */
    memory_write(&m, 0xFF01, 0x42); memory_write(&m, 0xFF02, 0x80);
    memory_tick(&m, 8192);
    CHECK(memory_read(&m, 0xFF01) == 0x42);
    CHECK(memory_read(&m, 0xFF02) == 0xFE);
    CHECK(memory_read(&m, 0xFF0F) == 0);
    /* Cancellation and restart do not deliver the abandoned byte's IRQ. */
    memory_write(&m, 0xFF02, 0x81); memory_tick(&m, 1000);
    memory_write(&m, 0xFF02, 0); memory_tick(&m, 8192);
    CHECK(memory_read(&m, 0xFF0F) == 0);
    memory_write(&m, 0xFF01, 0); memory_write(&m, 0xFF02, 0x81);
    memory_tick(&m, 4096); CHECK(memory_read(&m, 0xFF0F) == 8);
}

static void batches(void)
{
    Memory a, b; memory_init(&a); memory_init(&b);
    memory_write(&a, 0xFF01, 0x55); memory_write(&b, 0xFF01, 0x55);
    memory_write(&a, 0xFF02, 0x81); memory_write(&b, 0xFF02, 0x81);
    memory_tick(&a, 5000);
    for (unsigned i = 0; i < 5000; ++i) memory_tick(&b, 1);
    CHECK(memory_read(&a, 0xFF01) == memory_read(&b, 0xFF01));
    CHECK(memory_read(&a, 0xFF02) == memory_read(&b, 0xFF02));
    CHECK(memory_read(&a, 0xFF0F) == memory_read(&b, 0xFF0F));
}

static void polling_irq(bool halt, bool ime)
{
    Memory m; CPU cpu; memory_init(&m); reset_cpu(&cpu, &m);
    /* Synthetic program: send AA, wait for an IRQ-owned WRAM flag, read SB,
     * then write a progress marker. No retail ROM bytes are required. */
    static const uint8_t code[] = {
        0x3E,0xAA, 0xE0,0x01, 0xAF, 0xEA,0x00,0xC1,
        0x3E,0x81, 0xE0,0x02,
        0xFA,0x00,0xC1, 0xA7, 0x28,0xFA,
        0xF0,0x01, 0xEA,0x01,0xC1, 0x3E,0x42, 0xEA,0x02,0xC1, 0x76
    };
    static const uint8_t handler[] = {0xF5,0x3E,1,0xEA,0x00,0xC1,0xF1,0xD9};
    m.cartridge.rom_loaded = true;
    memcpy(m.cartridge.rom + 0x100, code, sizeof(code));
    memcpy(m.cartridge.rom + 0x58, handler, sizeof(handler));
    memory_write(&m, 0xFFFF, 8); cpu.ime = ime;
    if (halt) {
        /* Replace polling loop with HALT followed by NOP padding. */
        memset(m.cartridge.rom + 0x10C, 0, 6);
        m.cartridge.rom[0x10C] = 0x76;
    }
    bool visited_vector = false;
    for (unsigned i = 0; i < 3000 && memory_read(&m, 0xC102) != 0x42; ++i) {
        CHECK(cpu.state != CPU_LOCKED && cpu.state != CPU_STOPPED);
        cpu_step(&cpu, &m);
        visited_vector |= cpu.PC == 0x58;
    }
    CHECK(memory_read(&m, 0xC102) == 0x42);
    CHECK(memory_read(&m, 0xC101) == 0xFF);
    CHECK(visited_vector == ime);
    CHECK(memory_read(&m, 0xC100) == (ime ? 1 : 0));
    CHECK((memory_read(&m, 0xFF0F) & 8u) == (ime ? 0u : 8u));
    CHECK(cpu.SP == 0xFFFE);
}

static void stop_and_dma(void)
{
    Memory m; CPU cpu; memory_init(&m); reset_cpu(&cpu, &m);
    memory_write(&m, 0xFF01, 0); memory_write(&m, 0xFF02, 0x81);
    cpu.state = CPU_STOPPED;
    for (unsigned i = 0; i < 100; ++i) CHECK(cpu_step(&cpu, &m) == 0);
    CHECK(memory_read(&m, 0xFF01) == 0);
    memory_write(&m, 0xFF46, 0xC0); memory_tick(&m, 8);
    memory_cpu_write(&m, 0xFF02, 0); /* DMA blocks CPU cancellation. */
    CHECK(memory_read(&m, 0xFF02) & 0x80);
    memory_tick(&m, 4096 - 8);
    CHECK(memory_read(&m, 0xFF01) == 0xFF);
    CHECK(memory_read(&m, 0xFF0F) & 8);
}

int main(void)
{
    polling_irq(false, true); transfer_and_control(); batches();
    polling_irq(true, true); polling_irq(true, false); stop_and_dma();
    puts("Serial tests passed"); return 0;
}
