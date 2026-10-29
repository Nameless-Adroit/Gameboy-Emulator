#ifndef APU_H
#define APU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APU_CLOCK_HZ 4194304u
#define APU_SAMPLE_RATE 48000u
#define APU_BUFFER_FRAMES 4096u

/* Channel state contains no host resources. Register bytes remain owned by APU. */
typedef struct {
    bool enabled, dac, length_enabled;
    unsigned length, timer, position;
    unsigned volume, envelope_timer;
    bool envelope_running;
    unsigned sweep_shadow, sweep_timer;
    bool sweep_enabled, sweep_negated;
    uint16_t lfsr;
    uint8_t wave_sample;
} APUChannel;

typedef struct {
    bool powered;
    uint8_t regs[0x17]; /* FF10..FF26, including unused holes. */
    uint8_t wave[16];
    APUChannel channels[4];
    unsigned frame_step;
    unsigned sample_phase, accumulated_cycles;
    double accumulated[2], capacitor[2];
    int16_t samples[APU_BUFFER_FRAMES][2]; /* interleaved native-endian L/R */
    size_t read_index, buffered;
    uint64_t dropped_frames;
} APU;

void apu_init(APU *apu);
uint8_t apu_read(const APU *apu, uint16_t address);
void apu_write(APU *apu, uint16_t address, uint8_t value);
/* divider_before is Timer's existing 16-bit system divider, not another clock. */
void apu_tick(APU *apu, unsigned cycles, uint16_t divider_before);
void apu_div_reset(APU *apu, uint16_t divider_before);
/* Drain up to capacity stereo frames. No simulated time is advanced. */
size_t apu_read_samples(APU *apu, int16_t *interleaved, size_t capacity);

#endif
