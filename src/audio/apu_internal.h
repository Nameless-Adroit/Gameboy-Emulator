#ifndef APU_INTERNAL_H
#define APU_INTERNAL_H
#include "apu.h"
unsigned apu_channel_period(const APU *apu, unsigned channel);
void apu_channel_trigger(APU *apu, unsigned channel);
void apu_channel_tick(APU *apu, unsigned channel);
void apu_frame_clock(APU *apu);
void apu_mix_tick(APU *apu);
#endif
