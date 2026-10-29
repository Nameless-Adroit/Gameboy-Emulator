#include "apu_internal.h"
#include <string.h>

void apu_init(APU *apu) { memset(apu, 0, sizeof(*apu)); }

uint8_t apu_read(const APU *apu, uint16_t address)
{
    static const uint8_t masks[0x17] = {
        0x80, 0x3F, 0, 0xFF, 0xBF, 0xFF, 0x3F, 0, 0xFF, 0xBF,
        0x7F, 0xFF, 0x9F, 0xFF, 0xBF, 0xFF, 0xFF, 0, 0, 0xBF, 0, 0, 0x70
    };
    if (address >= 0xFF30 && address <= 0xFF3F) return apu->wave[address - 0xFF30];
    if (address < 0xFF10 || address > 0xFF26) return 0xFF;
    if (address == 0xFF26) {
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
        /* Unrestricted access is an intentional first-version simplification. */
        apu->wave[address - 0xFF30] = value;
        return;
    }
    if (address < 0xFF10 || address > 0xFF26) return;
    if (address == 0xFF26) {
        if (!(value & 0x80u)) power_off(apu);
        else if (!apu->powered) { apu->powered = true; apu->frame_step = 0; }
        return;
    }
    unsigned offset = address - 0xFF10u;
    if (offset == 5 || offset == 15) return; /* Unused NR20/NR40. */
    if (offset >= 20) {
        if (apu->powered) apu->regs[offset] = value;
        return;
    }
    unsigned i = offset / 5u, reg = offset % 5u;
    APUChannel *ch = &apu->channels[i];
    if (!apu->powered) {
        /* DMG permits length loading even with NR52 off; duty stays cleared. */
        if (reg == 1) ch->length = i == 2 ? 256u - value : 64u - (value & 63u);
        return;
    }
    if (offset == 0 && ch->sweep_negated && (apu->regs[0] & 8u) && !(value & 8u))
        ch->enabled = false;
    apu->regs[offset] = value;
    if (reg == 1) ch->length = i == 2 ? 256u - value : 64u - (value & 63u);
    if ((i == 2 && reg == 0) || (i != 2 && reg == 2)) {
        ch->dac = i == 2 ? (value & 0x80u) != 0 : (value & 0xF8u) != 0;
        if (!ch->dac) ch->enabled = false;
    }
    if (reg == 4) {
        bool enabled = (value & 0x40u) != 0;
        if (!ch->length_enabled && enabled && (apu->frame_step & 1u) && ch->length && !--ch->length)
            ch->enabled = false;
        ch->length_enabled = enabled;
        if (value & 0x80u) apu_channel_trigger(apu, i);
    }
}

void apu_div_reset(APU *apu, uint16_t divider_before)
{
    if (divider_before & 0x1000u) apu_frame_clock(apu);
}

void apu_tick(APU *apu, unsigned cycles, uint16_t divider_before)
{
    for (unsigned i = 0; i < cycles; ++i) {
        bool high = (divider_before & 0x1000u) != 0;
        divider_before = (uint16_t)(divider_before + 1u);
        if (high && !(divider_before & 0x1000u)) apu_frame_clock(apu);
        if (apu->powered)
            for (unsigned ch = 0; ch < 4; ++ch) apu_channel_tick(apu, ch);
        apu_mix_tick(apu);
    }
}
