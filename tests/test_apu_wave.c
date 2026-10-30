#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static void prepare(Memory *m)
{
    memory_init(m);
    memory_write(m, 0xFF26, 0x80);
    memory_write(m, 0xFF1A, 0x80);
    for (unsigned i = 0; i < 16; ++i)
        memory_write(m, (uint16_t)(0xFF30u + i), (uint8_t)(0x80u + i));
    /* Eight T-cycles per sample; startup adds six. NR32 stays muted,
     * which must not remove RAM restrictions or stop the sequencer. */
    memory_write(m, 0xFF1D, 0xFC);
}

static void access_windows(void)
{
    Memory m; prepare(&m);
    /* DAC enabled without triggering still permits normal addressed access. */
    for (unsigned i = 0; i < 16; ++i)
        CHECK(memory_read(&m, (uint16_t)(0xFF30u + i)) == 0x80u + i);
    memory_write(&m, 0xFF1E, 0x87);
    memory_tick(&m, 13);
    CHECK(m.apu.channels[2].position == 0);
    for (unsigned i = 0; i < 16; ++i) {
        CHECK(memory_read(&m, (uint16_t)(0xFF30u + i)) == 0xFF);
        memory_write(&m, (uint16_t)(0xFF30u + i), 0);
        CHECK(m.apu.wave[i] == 0x80u + i);
    }
    memory_tick(&m, 1); /* First fetch is nibble 1. */
    for (unsigned sample = 1; sample <= 32; ++sample) {
        unsigned pos = sample & 31u, index = pos / 2u;
        CHECK(m.apu.channels[2].position == pos);
        uint8_t latched = m.apu.channels[2].wave_sample;
        for (unsigned address = 0; address < 16; ++address) {
            uint8_t value = (uint8_t)(sample + address);
            CHECK(memory_read(&m, (uint16_t)(0xFF30u + address)) == m.apu.wave[index]);
            memory_write(&m, (uint16_t)(0xFF30u + address), value);
            CHECK(m.apu.wave[index] == value);
            CHECK(m.apu.channels[2].wave_sample == latched);
        }
        memory_tick(&m, 1); /* Second T-cycle of the access window. */
        CHECK(memory_read(&m, 0xFF3F) == m.apu.wave[index]);
        memory_tick(&m, 1);
        uint8_t before[16]; memcpy(before, m.apu.wave, sizeof(before));
        for (unsigned address = 0; address < 16; ++address) {
            CHECK(memory_read(&m, (uint16_t)(0xFF30u + address)) == 0xFF);
            memory_write(&m, (uint16_t)(0xFF30u + address), 0xFF);
        }
        CHECK(memcmp(before, m.apu.wave, sizeof(before)) == 0);
        memory_tick(&m, 6);
    }
    uint8_t retained = m.apu.channels[2].wave_sample;
    memory_write(&m, 0xFF1A, 0);
    memory_tick(&m, 100);
    CHECK(m.apu.channels[2].wave_sample == retained);
    memory_write(&m, 0xFF3F, 0xAB); CHECK(memory_read(&m, 0xFF3F) == 0xAB);
    memory_write(&m, 0xFF1A, 0x80); memory_write(&m, 0xFF1E, 0x87);
    CHECK(m.apu.channels[2].wave_sample == retained);
    memory_tick(&m, 13); CHECK(m.apu.channels[2].wave_sample == retained);
    memory_tick(&m, 1); CHECK(m.apu.channels[2].wave_sample == (m.apu.wave[0] & 15u));
    memory_write(&m, 0xFF26, 0);
    CHECK(m.apu.channels[2].wave_sample == 0);
    CHECK(memory_read(&m, 0xFF3F) == 0xAB);
    memory_write(&m, 0xFF26, 0x80);
    memory_write(&m, 0xFF1A, 0x80);
    memory_write(&m, 0xFF1B, 0xFF);
    memory_write(&m, 0xFF1E, 0xC0);
    memory_tick(&m, 8192); /* Length expiry restores addressed access with DAC on. */
    CHECK(!m.apu.channels[2].enabled && m.apu.channels[2].dac);
    memory_write(&m, 0xFF35, 0x23); CHECK(memory_read(&m, 0xFF35) == 0x23);
}

static void retrigger_windows(void)
{
    /* Sweep every sample position and every T-cycle within an eight-cycle
     * period, checking the complete RAM image, including wraparound. */
    for (unsigned pos = 0; pos < 32; ++pos) {
        for (unsigned phase = 0; phase < 8; ++phase) {
            Memory m; prepare(&m); memory_write(&m, 0xFF1E, 0x87);
            memory_tick(&m, 14u + (pos == 0 ? 31u : pos - 1u) * 8u + phase);
            CHECK(m.apu.channels[2].position == pos);
            uint8_t expected[16]; memcpy(expected, m.apu.wave, sizeof(expected));
            if (phase == 6) {
                unsigned next = ((pos + 1u) & 31u) / 2u;
                if (next < 4) expected[0] = expected[next];
                else memmove(expected, expected + (next & ~3u), 4);
            }
            uint8_t retained = m.apu.channels[2].wave_sample;
            memory_write(&m, 0xFF1E, 0x87);
            CHECK(memcmp(expected, m.apu.wave, sizeof(expected)) == 0);
            CHECK(m.apu.channels[2].position == 0);
            CHECK(m.apu.channels[2].wave_sample == retained);
            CHECK(memory_read(&m, 0xFF30) == 0xFF);
            memory_destroy(&m);
        }
    }
    /* Disabling before retrigger avoids corruption, even at the risky phase. */
    Memory m; prepare(&m); memory_write(&m, 0xFF1E, 0x87);
    memory_tick(&m, 14 + 8 * 17 + 6);
    uint8_t expected[16]; memcpy(expected, m.apu.wave, sizeof(expected));
    memory_write(&m, 0xFF1A, 0); memory_write(&m, 0xFF1A, 0x80);
    memory_write(&m, 0xFF1E, 0x87);
    CHECK(memcmp(expected, m.apu.wave, sizeof(expected)) == 0);
}

static void batches(void)
{
    Memory a, b; prepare(&a); prepare(&b);
    memory_write(&a, 0xFF1E, 0x87); memory_write(&b, 0xFF1E, 0x87);
    memory_tick(&a, 12345);
    for (unsigned i = 0; i < 12345; ++i) memory_tick(&b, 1);
    CHECK(memcmp(&a.apu, &b.apu, sizeof(APU)) == 0);
}

int main(void)
{
    access_windows(); retrigger_windows(); batches();
    puts("APU wave tests passed");
    return 0;
}
