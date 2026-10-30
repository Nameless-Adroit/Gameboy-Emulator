#include "apu_internal.h"

/* Produce a channel's four-bit generator level before DAC conversion.
 * Generator-off returns digital zero; DAC-off is handled separately below. */
static unsigned digital_output(const APU *apu, unsigned i)
{
    /* Bit 'position' is one phase of each eight-step duty waveform.
     * Set-bit counts give 1/8, 2/8, 4/8 and 6/8 high-level durations. */
    static const uint8_t duty[4] = {0x80, 0x81, 0xE1, 0x7E};
    const APUChannel *ch = &apu->channels[i];
    if (!ch->enabled) return 0;
    if (i < 2)
        return ((duty[apu->regs[i * 5u + 1u] >> 6] >> ch->position) & 1u) ? ch->volume : 0;
    if (i == 2) {
        /* NR32 codes 0/1/2/3 mean mute/full/half/quarter. This shifts the
         * digital nibble before the DAC; it is not an analog gain control. */
        unsigned level = (apu->regs[0x0C] >> 5) & 3u;
        return level ? (unsigned)ch->wave_sample >> (level - 1u) : 0;
    }
    /* Our XOR LFSR uses inverted bit 0 as the noise gate. */
    return (ch->lfsr & 1u) ? 0 : ch->volume;
}

/* Box-average the digital mix over each host sample interval, then approximate
 * the analog high-pass filter at 48 kHz. This is not a band-limited resampler. */
void apu_mix_tick(APU *apu)
{
    double mix[2] = {0, 0};
    if (apu->powered) {
        for (unsigned i = 0; i < 4; ++i) {
            /* An enabled DAC maps digital 0..15 to +15..-15 in mixer
             * units, even with an inactive generator. A disabled DAC makes
             * no contribution; this distinction supplies the DC offset. */
            if (!apu->channels[i].dac) continue;
            double value = 15.0 - 2.0 * (double)digital_output(apu, i);
            /* NR51 high nibble routes left, low nibble routes right. */
            if (apu->regs[0x15] & (1u << (i + 4u))) mix[0] += value;
            if (apu->regs[0x15] & (1u << i)) mix[1] += value;
        }
        /* NR50 gains are code+1, so zero means quietest rather than mute.
         * Scale 64 converts mixer units toward signed-16 PCM: four channels
         * at gain eight peak at 15*4*8*64 = 30720 before filtering. VIN bits
         * are intentionally ignored; no external audio source is modeled. */
        mix[0] *= (double)(((apu->regs[0x14] >> 4) & 7u) + 1u) * 64.0;
        mix[1] *= (double)((apu->regs[0x14] & 7u) + 1u) * 64.0;
    }
    apu->accumulated[0] += mix[0];
    apu->accumulated[1] += mix[1];
    ++apu->accumulated_cycles;
    /* Integer phase avoids rounding a sample to a fixed T-cycle interval:
     * 48 kHz does not divide 4194304 Hz. Carry the remainder into the next
     * interval so batching never changes the number of samples emitted. */
    apu->sample_phase += APU_SAMPLE_RATE;
    if (apu->sample_phase < APU_CLOCK_HZ) return;
    apu->sample_phase -= APU_CLOCK_HZ;
    /* Ring capacity is measured in stereo frames. Drop oldest output on
     * overflow so a missing/slow host consumer cannot stop the hardware. */
    if (apu->buffered == APU_BUFFER_FRAMES) {
        apu->read_index = (apu->read_index + 1u) % APU_BUFFER_FRAMES;
        --apu->buffered;
        ++apu->dropped_frames;
    }
    size_t index = (apu->read_index + apu->buffered) % APU_BUFFER_FRAMES;
    for (unsigned side = 0; side < 2; ++side) {
        /* Average every T-cycle in this interval, then remove slowly
         * varying DC. capacitor stores the low-frequency estimate carried
         * across samples; 0.9963 approximates DMG filtering at 48 kHz.
         * Power-off may leave a decaying tail rather than abrupt silence. */
        double input = apu->accumulated[side] / (double)apu->accumulated_cycles;
        double output = input - apu->capacitor[side];
        apu->capacitor[side] = input - output * 0.9963;
        /* Filter transients can exceed the nominal mix range. Clamp before
         * casting so conversion to signed-16 PCM always remains in range. */
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
    /* Copy complete L/R pairs, wrapping the read cursor. Hardware state and
     * sample-generation phase are untouched; only unread PCM is consumed. */
    size_t count = apu->buffered < capacity ? apu->buffered : capacity;
    for (size_t i = 0; i < count; ++i) {
        interleaved[i * 2u] = apu->samples[apu->read_index][0];
        interleaved[i * 2u + 1u] = apu->samples[apu->read_index][1];
        apu->read_index = (apu->read_index + 1u) % APU_BUFFER_FRAMES;
    }
    apu->buffered -= count;
    return count;
}
