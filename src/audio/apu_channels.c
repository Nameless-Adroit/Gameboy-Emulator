#include "apu_internal.h"
#include <string.h>

/* Register offsets of each channel's five-byte block. CH4 has no NRx0. */
static unsigned base(unsigned channel) { return channel * 5u; }

unsigned apu_channel_period(const APU *apu, unsigned channel)
{
    unsigned b = base(channel);
    if (channel == 3) {
        /* NR43: low three bits choose a divisor, upper nibble shifts it.
         * Divisor code zero is the special eight-T-cycle base period. */
        unsigned r = apu->regs[0x12];
        unsigned divisor = r & 7u;
        return (divisor ? divisor * 16u : 8u) << (r >> 4);
    }
    /* NRx3 plus NRx4's low bits form an 11-bit reload value. A higher
     * value shortens each step: pulse steps take 4*(2048-f), wave 2*(2048-f). */
    unsigned frequency = apu->regs[b + 3] | ((apu->regs[b + 4] & 7u) << 8);
    return (2048u - frequency) * (channel == 2 ? 2u : 4u);
}

/* Calculate from CH1's private shadow, not a potentially newer CPU write.
 * This helper has side effects even without committing a new frequency:
 * subtraction records negate use; addition overflow disables the channel. */
static unsigned sweep_calculate(APU *apu)
{
    APUChannel *ch = &apu->channels[0];
    unsigned delta = ch->sweep_shadow >> (apu->regs[0] & 7u);
    if (apu->regs[0] & 8u) {
        ch->sweep_negated = true;
        return ch->sweep_shadow - delta;
    }
    unsigned result = ch->sweep_shadow + delta;
    if (result > 2047) ch->enabled = false;
    return result;
}

void apu_channel_trigger(APU *apu, unsigned channel)
{
    APUChannel *ch = &apu->channels[channel];
    unsigned b = base(channel);
    /* Common DMG-B behavior: retriggering one 2-T-cycle APU clock before
     * the next fetch corrupts RAM using that fetch's address. This is a
     * different window from CPU access to the byte just fetched.
     * Pan Docs Audio Details; blargg dmg_sound test 10. */
    if (channel == 2 && ch->enabled && ch->timer == 2) {
        unsigned index = ((ch->position + 1u) & 31u) / 2u;
        if (index < 4) apu->wave[0] = apu->wave[index];
        else memmove(apu->wave, apu->wave + (index & ~3u), 4);
    }
    /* Trigger can start the generator only if its DAC is powered. Loading
     * the remaining runtime state is still allowed when the DAC is off. */
    ch->enabled = ch->dac;
    if (!ch->length) {
        ch->length = channel == 2 ? 256u : 64u;
        /* When the next sequencer step does not clock length, the reload loses
         * one count immediately if length is enabled. */
        if (ch->length_enabled && (apu->frame_step & 1u)) --ch->length;
    }
    /* Pulse duty position deliberately survives trigger: repeatedly
     * retriggering restarts the timer, not the eight-step waveform phase. */
    ch->timer = apu_channel_period(apu, channel);
    if (channel == 2) {
        ch->position = 0; /* Sample buffer intentionally survives a retrigger. */
        ch->timer += 6; /* Startup latency before the first sample fetch. */
        ch->wave_access_cycles = 0;
    } else {
        ch->volume = apu->regs[b + 2] >> 4;
        ch->envelope_timer = apu->regs[b + 2] & 7u;
        /* Zero pace loads an eight-tick countdown, but volume updates are
         * suppressed later by the envelope clock when pace remains zero. */
        if (!ch->envelope_timer) ch->envelope_timer = 8;
        ch->envelope_running = true;
    }
    /* XOR feedback plus inverted bit-0 output uses the complemented form
     * of an all-zero XNOR generator, hence this all-ones reset state. */
    if (channel == 3) ch->lfsr = 0x7FFF;
    if (channel == 0) {
        /* Sweep keeps its own frequency copy; later CPU frequency writes
         * do not update this shadow until another trigger. */
        ch->sweep_shadow = apu->regs[3] | ((apu->regs[4] & 7u) << 8);
        ch->sweep_timer = (apu->regs[0] >> 4) & 7u;
        if (!ch->sweep_timer) ch->sweep_timer = 8;
        ch->sweep_enabled = (apu->regs[0] & 0x77u) != 0;
        ch->sweep_negated = false;
        /* Nonzero shift checks overflow immediately, even with pace zero.
         * The calculated value is checked here but not committed. */
        if (apu->regs[0] & 7u) (void)sweep_calculate(apu);
    }
}

void apu_channel_tick(APU *apu, unsigned channel)
{
    APUChannel *ch = &apu->channels[channel];
    if (channel == 2) {
        if (ch->wave_access_cycles) --ch->wave_access_cycles;
        /* Inactive CH3 must not fetch RAM or overwrite its retained sample. */
        if (!ch->enabled) return;
    }
    /* Register frequency writes affect the next reload, preserving the
     * remainder of the current step. A trigger explicitly reloads sooner. */
    if (ch->timer) --ch->timer;
    if (ch->timer) return;
    ch->timer = apu_channel_period(apu, channel);
    if (channel < 2) ch->position = (ch->position + 1u) & 7u;
    else if (channel == 2) {
        /* Advance before fetching: a newly triggered wave channel starts
         * with its retained buffer, then fetches nibble 1 on first expiry.
         * Even indices select the high nibble; odd indices the low nibble. */
        ch->position = (ch->position + 1u) & 31u;
        unsigned byte = apu->wave[ch->position / 2u];
        ch->wave_sample = (uint8_t)((ch->position & 1u) ? byte & 15u : byte >> 4);
        ch->wave_access_cycles = 2;
    } else {
        /* NR43 shifts 14/15 suppress LFSR steps in this DMG model. */
        if ((apu->regs[0x12] >> 4) >= 14) return;
        /* XOR the two low bits and feed bit 14 of the 15-bit register.
         * Short mode additionally overwrites bit 6 with the same feedback;
         * it does not simply mask the stored register down to seven bits. */
        unsigned feedback = (ch->lfsr ^ (ch->lfsr >> 1)) & 1u;
        ch->lfsr = (uint16_t)((ch->lfsr >> 1) | (feedback << 14));
        if (apu->regs[0x12] & 8u)
            ch->lfsr = (uint16_t)((ch->lfsr & ~0x40u) | (feedback << 6));
    }
}

void apu_frame_clock(APU *apu)
{
    if (!apu->powered) return;
    /* The eight-step sequencer advances on DIV edges, not PCM samples.
     * Steps:          0 1 2 3 4 5 6 7
     * length (256Hz)  x   x   x   x
     * sweep  (128Hz)      x       x
     * volume ( 64Hz)                x */
    unsigned step = apu->frame_step;
    if (!(step & 1u)) {
        for (unsigned i = 0; i < 4; ++i) {
            APUChannel *ch = &apu->channels[i];
            /* Expiry stops generation but leaves DAC power unchanged. */
            if (ch->length_enabled && ch->length && !--ch->length)
                ch->enabled = false;
        }
    }
    if (step == 2 || step == 6) {
        APUChannel *ch = &apu->channels[0];
        if (ch->sweep_timer && !--ch->sweep_timer) {
            unsigned pace = (apu->regs[0] >> 4) & 7u;
            ch->sweep_timer = pace ? pace : 8u;
            /* A zero pace reloads the countdown as eight but suppresses
             * periodic frequency updates. Trigger overflow checks differ. */
            if (ch->sweep_enabled && pace) {
                unsigned value = sweep_calculate(apu);
                if (value <= 2047 && (apu->regs[0] & 7u)) {
                    /* Commit one result to both shadow and NRx3/NRx4;
                     * retain NRx4's control bits when replacing high bits. */
                    ch->sweep_shadow = value;
                    apu->regs[3] = (uint8_t)value;
                    apu->regs[4] = (uint8_t)((apu->regs[4] & 0xF8u) | (value >> 8));
                    (void)sweep_calculate(apu); /* Second overflow check. */
                }
            }
        }
    }
    if (step == 7) {
        for (unsigned i = 0; i < 4; ++i) {
            if (i == 2) continue; /* CH3 has level shifts, no envelope. */
            APUChannel *ch = &apu->channels[i];
            unsigned envelope = apu->regs[base(i) + 2];
            if (!ch->envelope_running || !ch->envelope_timer) continue;
            if (--ch->envelope_timer) continue;
            unsigned pace = envelope & 7u;
            ch->envelope_timer = pace ? pace : 8u;
            if (!pace) continue;
            /* Saturate at the four-bit endpoint. The generator stays on
             * even when volume reaches zero; NRx2's stored value is unchanged. */
            if ((envelope & 8u) && ch->volume < 15) ++ch->volume;
            else if (!(envelope & 8u) && ch->volume) --ch->volume;
            else ch->envelope_running = false;
        }
    }
    apu->frame_step = (step + 1u) & 7u;
}
