#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static void power(Memory *m) { memory_init(m); memory_write(m, 0xFF26, 0x80); }
static void pulse(Memory *m, unsigned i, unsigned frequency)
{
    uint16_t b = (uint16_t)(0xFF10u + i * 5u);
    memory_write(m, (uint16_t)(b + 1), 0x80);
    memory_write(m, (uint16_t)(b + 2), 0xF1);
    memory_write(m, (uint16_t)(b + 3), (uint8_t)frequency);
    memory_write(m, (uint16_t)(b + 4), (uint8_t)(0x80u | (frequency >> 8)));
}

static void registers(void)
{
    Memory m; memory_init(&m);
    static const uint8_t reset[0x20] = {
        0x80,0x3F,0,0xFF,0xBF,0xFF,0x3F,0,0xFF,0xBF,
        0x7F,0xFF,0x9F,0xFF,0xBF,0xFF,0xFF,0,0,0xBF,0,0,0x70,
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
    };
    for (unsigned i = 0; i < 0x20; ++i)
        CHECK(memory_read(&m, (uint16_t)(0xFF10u + i)) == reset[i]);
    memory_write(&m, 0xFF12, 0xF3); CHECK(memory_read(&m, 0xFF12) == 0);
    memory_write(&m, 0xFF11, 0xFF); CHECK(m.apu.channels[0].length == 1);
    CHECK(memory_read(&m, 0xFF11) == 0x3F);
    for (unsigned i = 0; i < 16; ++i) {
        memory_write(&m, (uint16_t)(0xFF30u + i), (uint8_t)(i * 17u));
        CHECK(memory_read(&m, (uint16_t)(0xFF30u + i)) == i * 17u);
    }
    memory_write(&m, 0xFF26, 0x8F); CHECK(memory_read(&m, 0xFF26) == 0xF0);
    pulse(&m, 0, 1000); CHECK(memory_read(&m, 0xFF26) == 0xF1);
    memory_write(&m, 0xFF26, 0x80); CHECK(memory_read(&m, 0xFF26) == 0xF1);
    memory_write(&m, 0xFF12, 0); CHECK(memory_read(&m, 0xFF26) == 0xF0);
    pulse(&m, 1, 1000); memory_write(&m, 0xFF26, 0);
    CHECK(memory_read(&m, 0xFF26) == 0x70 && memory_read(&m, 0xFF17) == 0);
    CHECK(memory_read(&m, 0xFF3F) == 0xFF);
    CHECK(m.timer.divider == 0 && m.apu.buffered == 0);
}

static void channel_timers(void)
{
    Memory m; power(&m); pulse(&m, 0, 2047); pulse(&m, 1, 2046);
    memory_tick(&m, 3); CHECK(m.apu.channels[0].position == 0);
    memory_tick(&m, 1); CHECK(m.apu.channels[0].position == 1);
    CHECK(m.apu.channels[1].position == 0);
    memory_tick(&m, 4); CHECK(m.apu.channels[1].position == 1);
    /* Retrigger resets timer, preserving pulse phase. */
    memory_write(&m, 0xFF14, 0x87); CHECK(m.apu.channels[0].position == 2);
    memory_write(&m, 0xFF30, 0xA5); memory_write(&m, 0xFF31, 0xC3);
    memory_write(&m, 0xFF1A, 0x80); memory_write(&m, 0xFF1C, 0x20);
    memory_write(&m, 0xFF1D, 0xFF); memory_write(&m, 0xFF1E, 0x87);
    memory_tick(&m, 2); CHECK(m.apu.channels[2].wave_sample == 5);
    memory_tick(&m, 2); CHECK(m.apu.channels[2].wave_sample == 12);
    memory_write(&m, 0xFF1A, 0); CHECK(!m.apu.channels[2].enabled);
    memory_write(&m, 0xFF21, 0xF0); memory_write(&m, 0xFF22, 0);
    memory_write(&m, 0xFF23, 0x80);
    memory_tick(&m, 7); CHECK(m.apu.channels[3].lfsr == 0x7FFF);
    memory_tick(&m, 1); CHECK(m.apu.channels[3].lfsr == 0x3FFF);
    memory_write(&m, 0xFF22, 8); memory_write(&m, 0xFF23, 0x80);
    memory_tick(&m, 8); CHECK(m.apu.channels[3].lfsr == 0x3FBF);
}

static void sequencing(void)
{
    Memory m; power(&m); pulse(&m, 0, 1000);
    memory_write(&m, 0xFF11, 0x3F); memory_write(&m, 0xFF14, 0x43);
    memory_tick(&m, 8191); CHECK(m.apu.channels[0].enabled);
    memory_tick(&m, 1); CHECK(!m.apu.channels[0].enabled && m.apu.frame_step == 1);
    /* Enabling length on a non-length phase immediately decrements it. */
    pulse(&m, 0, 1000); memory_write(&m, 0xFF11, 0x3F);
    memory_write(&m, 0xFF14, 0x43); CHECK(!m.apu.channels[0].enabled);
    memory_write(&m, 0xFF14, 0xC3); CHECK(m.apu.channels[0].length == 63);
    power(&m); pulse(&m, 0, 1000);
    memory_write(&m, 0xFF12, 0x29); memory_write(&m, 0xFF14, 0x83);
    memory_tick(&m, 65535); CHECK(m.apu.channels[0].volume == 2);
    memory_tick(&m, 1); CHECK(m.apu.channels[0].volume == 3);
    power(&m); memory_write(&m, 0xFF10, 0x11); pulse(&m, 0, 1000);
    memory_tick(&m, 24576);
    CHECK(m.apu.channels[0].sweep_shadow == 1500 && !m.apu.channels[0].enabled);
    power(&m); memory_write(&m, 0xFF10, 0x01); pulse(&m, 0, 1800);
    CHECK(!m.apu.channels[0].enabled); /* Trigger overflow even with pace zero. */
    power(&m); pulse(&m, 0, 1000); memory_write(&m, 0xFF11, 0x3F);
    memory_write(&m, 0xFF14, 0x43); memory_tick(&m, 2048);
    CHECK(m.apu.channels[0].enabled);
    memory_write(&m, 0xFF04, 0); /* Internal DIV bit 12 is low: no APU edge. */
    CHECK(m.apu.channels[0].enabled && m.apu.frame_step == 0);
    memory_tick(&m, 4096); memory_write(&m, 0xFF04, 0);
    CHECK(!m.apu.channels[0].enabled && m.apu.frame_step == 1);
}

static void mixing_and_batches(void)
{
    Memory a, b; power(&a); pulse(&a, 0, 1750);
    memory_write(&a, 0xFF24, 0x77); memory_write(&a, 0xFF25, 0x10); /* Left only. */
    power(&b); pulse(&b, 0, 1750);
    memory_write(&b, 0xFF24, 0x77); memory_write(&b, 0xFF25, 0x10);
    memory_tick(&a, 70000);
    for (unsigned i = 0; i < 70000; ++i) memory_tick(&b, 1);
    CHECK(memcmp(&a.apu, &b.apu, sizeof(APU)) == 0);
    CHECK(a.apu.buffered == (uint64_t)70000u * APU_SAMPLE_RATE / APU_CLOCK_HZ);
    int16_t pcm[2048]; size_t count = apu_read_samples(&a.apu, pcm, 1024);
    bool positive = false, negative = false;
    for (size_t i = 0; i < count; ++i) {
        CHECK(pcm[i * 2u + 1u] == 0);
        positive |= pcm[i * 2u] > 0; negative |= pcm[i * 2u] < 0;
    }
    CHECK(positive && negative && a.apu.buffered == 0);
    /* Exact rational sample cadence, including when powered off. */
    memory_init(&a); memory_tick(&a, APU_CLOCK_HZ);
    CHECK(a.apu.buffered == APU_BUFFER_FRAMES);
    CHECK(a.apu.dropped_frames == APU_SAMPLE_RATE - APU_BUFFER_FRAMES);
    CHECK(a.apu.sample_phase == 0);
    while ((count = apu_read_samples(&a.apu, pcm, 1024)) != 0)
        for (size_t i = 0; i < count * 2u; ++i) CHECK(pcm[i] == 0);
}

static void all_channel_controls(void)
{
    Memory m; power(&m);
    static const uint16_t length_reg[4] = {0xFF11,0xFF16,0xFF1B,0xFF20};
    static const uint16_t dac_reg[4] = {0xFF12,0xFF17,0xFF1A,0xFF21};
    static const uint16_t trigger_reg[4] = {0xFF14,0xFF19,0xFF1E,0xFF23};
    for (unsigned i = 0; i < 4; ++i) {
        memory_write(&m, length_reg[i], 0xFF);
        memory_write(&m, dac_reg[i], 0xF0);
        memory_write(&m, trigger_reg[i], 0xC0);
    }
    CHECK((memory_read(&m, 0xFF26) & 15) == 15);
    memory_tick(&m, 8192);
    CHECK((memory_read(&m, 0xFF26) & 15) == 0);
    for (unsigned i = 0; i < 4; ++i) {
        memory_write(&m, trigger_reg[i], 0x80);
        CHECK(m.apu.channels[i].length == (i == 2 ? 256u : 64u));
        memory_write(&m, dac_reg[i], 0);
        CHECK(!m.apu.channels[i].enabled);
        memory_write(&m, trigger_reg[i], 0x80);
        CHECK(!m.apu.channels[i].enabled); /* A trigger cannot override DAC off. */
    }
    power(&m); pulse(&m, 1, 1500);
    memory_write(&m, 0xFF17, 0x11); memory_write(&m, 0xFF19, 0x85);
    memory_tick(&m, 65536); CHECK(m.apu.channels[1].volume == 0);
    CHECK(m.apu.channels[1].enabled && m.apu.channels[1].dac);
    CHECK(memory_read(&m, 0xFF17) == 0x11); /* Envelope does not rewrite NR22. */
    power(&m); memory_write(&m, 0xFF10, 0x19); pulse(&m, 0, 1000);
    memory_tick(&m, 24576); CHECK(m.apu.channels[0].sweep_shadow == 500);
    memory_write(&m, 0xFF10, 0x11); CHECK(!m.apu.channels[0].enabled);
    /* Maximum noise shifts stop LFSR clocking. */
    memory_write(&m, 0xFF21, 0xF0); memory_write(&m, 0xFF22, 0xE0);
    memory_write(&m, 0xFF23, 0x80); memory_tick(&m, 131072);
    CHECK(m.apu.channels[3].lfsr == 0x7FFF);
}

static void stereo_volume(void)
{
    Memory m; power(&m); pulse(&m, 1, 1750);
    memory_write(&m, 0xFF25, 0x22);
    memory_write(&m, 0xFF24, 0x70); /* left 8/8, right 1/8, not muted */
    memory_tick(&m, 10000);
    int16_t pcm[512]; size_t count = apu_read_samples(&m.apu, pcm, 256);
    bool nonzero = false;
    for (size_t i = 0; i < count; ++i) {
        int delta = (int)pcm[i * 2u] - (int)pcm[i * 2u + 1u] * 8;
        CHECK(delta >= -8 && delta <= 8); /* Integer truncation at final PCM. */
        nonzero |= pcm[i * 2u + 1u] != 0;
    }
    CHECK(nonzero);
}

static void cpu_bus(void)
{
    Memory m; CPU cpu; power(&m); reset_cpu(&cpu, &m);
    cpu.PC = 0xC000; cpu.A = 0xF0;
    m.wram[0] = 0xE0; m.wram[1] = 0x12; /* LDH (NR12),A */
    CHECK(cpu_step(&cpu, &m) == 12);
    CHECK(m.timer.divider == 12 && m.apu.sample_phase == 12u * APU_SAMPLE_RATE);
    CHECK(memory_read(&m, 0xFF12) == 0xF0);
    memory_write(&m, 0xFF24, 0x77); CHECK(m.timer.divider == 12);
    cpu.state = CPU_HALTED; cpu.ime = false; m.ie = 0; m.io[0x0F] = 0;
    CHECK(cpu_step(&cpu, &m) == 4 && m.timer.divider == 16);
    cpu.state = CPU_STOPPED;
    CHECK(cpu_step(&cpu, &m) == 0 && m.timer.divider == 16);
    memory_write(&m, 0xFF46, 0xC0); memory_tick(&m, 8);
    memory_cpu_write(&m, 0xFF12, 0); CHECK(memory_read(&m, 0xFF12) == 0xF0);
}

int main(void)
{
    registers(); channel_timers(); sequencing(); mixing_and_batches(); all_channel_controls(); stereo_volume(); cpu_bus();
    puts("APU tests passed"); return 0;
}
