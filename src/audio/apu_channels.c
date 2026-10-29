#include "apu_internal.h"

/* Register offsets of each channel's five-byte block. CH4 has no NRx0. */
static unsigned base(unsigned channel) { return channel * 5u; }

unsigned apu_channel_period(const APU *apu, unsigned channel)
{
    unsigned b = base(channel);
    if (channel == 3) {
        unsigned r = apu->regs[0x12];
        unsigned divisor = r & 7u;
        return (divisor ? divisor * 16u : 8u) << (r >> 4);
    }
    unsigned frequency = apu->regs[b + 3] | ((apu->regs[b + 4] & 7u) << 8);
    return (2048u - frequency) * (channel == 2 ? 2u : 4u);
}

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
    ch->enabled = ch->dac;
    if (!ch->length) {
        ch->length = channel == 2 ? 256u : 64u;
        /* When the next sequencer step does not clock length, the reload loses
         * one count immediately if length is enabled. */
        if (ch->length_enabled && (apu->frame_step & 1u)) --ch->length;
    }
    ch->timer = apu_channel_period(apu, channel);
    if (channel == 2) {
        ch->position = 0; /* Sample buffer intentionally survives a retrigger. */
    } else {
        ch->volume = apu->regs[b + 2] >> 4;
        ch->envelope_timer = apu->regs[b + 2] & 7u;
        if (!ch->envelope_timer) ch->envelope_timer = 8;
        ch->envelope_running = true;
    }
    if (channel == 3) ch->lfsr = 0x7FFF;
    if (channel == 0) {
        ch->sweep_shadow = apu->regs[3] | ((apu->regs[4] & 7u) << 8);
        ch->sweep_timer = (apu->regs[0] >> 4) & 7u;
        if (!ch->sweep_timer) ch->sweep_timer = 8;
        ch->sweep_enabled = (apu->regs[0] & 0x77u) != 0;
        ch->sweep_negated = false;
        if (apu->regs[0] & 7u) (void)sweep_calculate(apu);
    }
}

void apu_channel_tick(APU *apu, unsigned channel)
{
    APUChannel *ch = &apu->channels[channel];
    if (ch->timer) --ch->timer;
    if (ch->timer) return;
    ch->timer = apu_channel_period(apu, channel);
    if (channel < 2) ch->position = (ch->position + 1u) & 7u;
    else if (channel == 2) {
        ch->position = (ch->position + 1u) & 31u;
        unsigned byte = apu->wave[ch->position / 2u];
        ch->wave_sample = (uint8_t)((ch->position & 1u) ? byte & 15u : byte >> 4);
    } else {
        if ((apu->regs[0x12] >> 4) >= 14) return;
        unsigned feedback = (ch->lfsr ^ (ch->lfsr >> 1)) & 1u;
        ch->lfsr = (uint16_t)((ch->lfsr >> 1) | (feedback << 14));
        if (apu->regs[0x12] & 8u)
            ch->lfsr = (uint16_t)((ch->lfsr & ~0x40u) | (feedback << 6));
    }
}

void apu_frame_clock(APU *apu)
{
    if (!apu->powered) return;
    unsigned step = apu->frame_step;
    if (!(step & 1u)) {
        for (unsigned i = 0; i < 4; ++i) {
            APUChannel *ch = &apu->channels[i];
            if (ch->length_enabled && ch->length && !--ch->length)
                ch->enabled = false;
        }
    }
    if (step == 2 || step == 6) {
        APUChannel *ch = &apu->channels[0];
        if (ch->sweep_timer && !--ch->sweep_timer) {
            unsigned pace = (apu->regs[0] >> 4) & 7u;
            ch->sweep_timer = pace ? pace : 8u;
            if (ch->sweep_enabled && pace) {
                unsigned value = sweep_calculate(apu);
                if (value <= 2047 && (apu->regs[0] & 7u)) {
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
            if (i == 2) continue;
            APUChannel *ch = &apu->channels[i];
            unsigned envelope = apu->regs[base(i) + 2];
            if (!ch->envelope_running || !ch->envelope_timer) continue;
            if (--ch->envelope_timer) continue;
            unsigned pace = envelope & 7u;
            ch->envelope_timer = pace ? pace : 8u;
            if (!pace) continue;
            if ((envelope & 8u) && ch->volume < 15) ++ch->volume;
            else if (!(envelope & 8u) && ch->volume) --ch->volume;
            else ch->envelope_running = false;
        }
    }
    apu->frame_step = (step + 1u) & 7u;
}
