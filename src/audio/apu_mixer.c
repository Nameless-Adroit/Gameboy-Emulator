#include "apu_internal.h"

static unsigned digital_output(const APU *apu, unsigned i)
{
    static const uint8_t duty[4] = {0x80, 0x81, 0xE1, 0x7E};
    const APUChannel *ch = &apu->channels[i];
    if (!ch->enabled) return 0;
    if (i < 2)
        return ((duty[apu->regs[i * 5u + 1u] >> 6] >> ch->position) & 1u) ? ch->volume : 0;
    if (i == 2) {
        unsigned level = (apu->regs[0x0C] >> 5) & 3u;
        return level ? (unsigned)ch->wave_sample >> (level - 1u) : 0;
    }
    return (ch->lfsr & 1u) ? 0 : ch->volume;
}

/* Box-average the digital mix over each host sample interval, then approximate
 * the analog high-pass filter at 48 kHz. This is not a band-limited resampler. */
void apu_mix_tick(APU *apu)
{
    double mix[2] = {0, 0};
    if (apu->powered) {
        for (unsigned i = 0; i < 4; ++i) {
            if (!apu->channels[i].dac) continue;
            double value = 15.0 - 2.0 * (double)digital_output(apu, i);
            if (apu->regs[0x15] & (1u << (i + 4u))) mix[0] += value;
            if (apu->regs[0x15] & (1u << i)) mix[1] += value;
        }
        mix[0] *= (double)(((apu->regs[0x14] >> 4) & 7u) + 1u) * 64.0;
        mix[1] *= (double)((apu->regs[0x14] & 7u) + 1u) * 64.0;
    }
    apu->accumulated[0] += mix[0];
    apu->accumulated[1] += mix[1];
    ++apu->accumulated_cycles;
    apu->sample_phase += APU_SAMPLE_RATE;
    if (apu->sample_phase < APU_CLOCK_HZ) return;
    apu->sample_phase -= APU_CLOCK_HZ;
    if (apu->buffered == APU_BUFFER_FRAMES) {
        apu->read_index = (apu->read_index + 1u) % APU_BUFFER_FRAMES;
        --apu->buffered;
        ++apu->dropped_frames;
    }
    size_t index = (apu->read_index + apu->buffered) % APU_BUFFER_FRAMES;
    for (unsigned side = 0; side < 2; ++side) {
        double input = apu->accumulated[side] / (double)apu->accumulated_cycles;
        double output = input - apu->capacitor[side];
        apu->capacitor[side] = input - output * 0.9963;
        if (output > 32767.0) output = 32767.0;
        if (output < -32768.0) output = -32768.0;
        apu->samples[index][side] = (int16_t)output;
        apu->accumulated[side] = 0;
    }
    apu->accumulated_cycles = 0;
    ++apu->buffered;
}

size_t apu_read_samples(APU *apu, int16_t *interleaved, size_t capacity)
{
    size_t count = apu->buffered < capacity ? apu->buffered : capacity;
    for (size_t i = 0; i < count; ++i) {
        interleaved[i * 2u] = apu->samples[apu->read_index][0];
        interleaved[i * 2u + 1u] = apu->samples[apu->read_index][1];
        apu->read_index = (apu->read_index + 1u) % APU_BUFFER_FRAMES;
    }
    apu->buffered -= count;
    return count;
}
