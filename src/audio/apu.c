#include "apu_internal.h"
#include <string.h>

/* The owner can reset the entire self-contained core without host cleanup. */
void apu_init(APU *apu) { memset(apu, 0, sizeof(*apu)); }

uint8_t apu_read(const APU *apu, uint16_t address)
{
    /* OR masks model read-as-one bits and write-only fields, rather than
     * exposing every stored register byte. Offsets are address - FF10. */
    static const uint8_t masks[0x17] = {
        0x80, 0x3F, 0, 0xFF, 0xBF, 0xFF, 0x3F, 0, 0xFF, 0xBF,
        0x7F, 0xFF, 0x9F, 0xFF, 0xBF, 0xFF, 0xFF, 0, 0, 0xBF, 0, 0, 0x70
    };
    if (address >= 0xFF30 && address <= 0xFF3F) {
        const APUChannel *ch = &apu->channels[2];
        if (ch->enabled)
            return ch->wave_access_cycles ? apu->wave[ch->position / 2u] : 0xFF;
        return apu->wave[address - 0xFF30];
    }
    if (address < 0xFF10 || address > 0xFF26) return 0xFF;
    if (address == 0xFF26) {
        /* NR52 low bits report live generator activity, not writable latches
         * and not DAC power. Bits 4..6 always read high. */
        unsigned status = apu->powered ? 0x80u : 0;
        for (unsigned i = 0; i < 4; ++i)
            if (apu->channels[i].enabled) status |= 1u << i;
        return (uint8_t)(status | 0x70u);
    }
    return apu->regs[address - 0xFF10] | masks[address - 0xFF10];
}

static void power_off(APU *apu)
{
    /* DMG length counters and wave RAM survive power-off. Host sample state is
     * also retained: NR52 must not reset sample rate phase or the shared DIV. */
    for (unsigned i = 0; i < 4; ++i) {
        unsigned length = apu->channels[i].length;
        memset(&apu->channels[i], 0, sizeof(apu->channels[i]));
        apu->channels[i].length = length;
    }
    memset(apu->regs, 0, sizeof(apu->regs));
    apu->powered = false;
    apu->frame_step = 0;
}

void apu_write(APU *apu, uint16_t address, uint8_t value)
{
    if (address >= 0xFF30 && address <= 0xFF3F) {
        /* On DMG the CPU shares CH3's selected address during its fetch only. */
        const APUChannel *ch = &apu->channels[2];
        if (!ch->enabled) apu->wave[address - 0xFF30] = value;
        else if (ch->wave_access_cycles) apu->wave[ch->position / 2u] = value;
        return;
    }
    if (address < 0xFF10 || address > 0xFF26) return;
    if (address == 0xFF26) {
        /* Only master power is writable. Rewriting 'on' must not retrigger
         * channels or restart the sequencer; status bits are ignored. */
        if (!(value & 0x80u)) power_off(apu);
        else if (!apu->powered) { apu->powered = true; apu->frame_step = 0; }
        return;
    }
    unsigned offset = address - 0xFF10u;
    if (offset == 5 || offset == 15) return; /* Unused NR20/NR40. */
    /* NR50/NR51 are global mixer registers, outside the channel blocks. */
    if (offset >= 20) {
        if (apu->powered) apu->regs[offset] = value;
        return;
    }
    /* FF10..FF23 divides into five-byte channel blocks. CH2/CH4 have
     * unused first slots; CH3 uses its first slot for DAC enable instead. */
    unsigned i = offset / 5u, reg = offset % 5u;
    APUChannel *ch = &apu->channels[i];
    if (!apu->powered) {
        /* DMG permits length loading even with NR52 off; duty stays cleared. */
        if (reg == 1) ch->length = i == 2 ? 256u - value : 64u - (value & 63u);
        return;
    }
    /* After CH1 has performed a subtracting sweep calculation, clearing
     * NR10's negate bit disables that generator. Other writes keep running. */
    if (offset == 0 && ch->sweep_negated && (apu->regs[0] & 8u) && !(value & 8u))
        ch->enabled = false;
    apu->regs[offset] = value;
    /* NRx1 stores an inverse duration: CH3 has eight length bits, others six. */
    if (reg == 1) ch->length = i == 2 ? 256u - value : 64u - (value & 63u);
    /* CH3 uses NR30 bit 7; the other DACs use their envelope's upper five
     * bits. Disabling a DAC silences the generator immediately. */
    if ((i == 2 && reg == 0) || (i != 2 && reg == 2)) {
        ch->dac = i == 2 ? (value & 0x80u) != 0 : (value & 0xF8u) != 0;
        if (!ch->dac) ch->enabled = false;
    }
    if (reg == 4) {
        /* Length enable takes effect before trigger handling. Enabling it
         * when the next sequencer step is odd incurs an immediate decrement;
         * a trigger can then reload an expired counter in the helper. */
        bool enabled = (value & 0x40u) != 0;
        if (!ch->length_enabled && enabled && (apu->frame_step & 1u) && ch->length && !--ch->length)
            ch->enabled = false;
        ch->length_enabled = enabled;
        if (value & 0x80u) apu_channel_trigger(apu, i);
    }
}

void apu_div_reset(APU *apu, uint16_t divider_before)
{
    /* DIV exposes the upper byte: its bit 4 is internal divider bit 12.
     * Resetting while that bit is already low does not create a falling edge. */
    if (divider_before & 0x1000u) apu_frame_clock(apu);
}

void apu_tick(APU *apu, unsigned cycles, uint16_t divider_before)
{
    /* Walk each supplied T-cycle so a large batch cannot skip sequencer
     * edges, oscillator transitions or PCM boundaries. The uint16_t cast
     * mirrors the existing hardware divider's wraparound. */
    for (unsigned i = 0; i < cycles; ++i) {
        bool high = (divider_before & 0x1000u) != 0;
        divider_before = (uint16_t)(divider_before + 1u);
        if (high && !(divider_before & 0x1000u)) apu_frame_clock(apu);
        if (apu->powered)
            for (unsigned ch = 0; ch < 4; ++ch) apu_channel_tick(apu, ch);
        /* Keep PCM cadence even with NR52 off; the filter tail may decay
         * before reaching silence. Host mute never changes hardware timing. */
        apu_mix_tick(apu);
    }
}
