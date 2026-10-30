#ifndef APU_INTERNAL_H
#define APU_INTERNAL_H
#include "apu.h"

/* Private interface shared by the register, channel and mixer modules.
 * Channel indices 0..3 mean hardware CH1..CH4. These helpers mutate only APU
 * state; only the caller of apu_tick() supplies elapsed emulated time. */
/* Timer reload in CPU T-cycles: one duty step, wave nibble or LFSR step.
 * This is a waveform-step period, not the duration of a complete tone cycle. */
unsigned apu_channel_period(const APU *apu, unsigned channel);
/* Apply NRx4 bit 7: restart the channel's applicable timer/envelope/sweep
 * state. A disabled DAC prevents activation; an expired length is reloaded. */
void apu_channel_trigger(APU *apu, unsigned channel);
/* Consume one T-cycle of an oscillator; reload and advance on expiry.
 * Called only while powered, including for inactive channels in this model. */
void apu_channel_tick(APU *apu, unsigned channel);
/* Consume one 512 Hz DIV falling edge. frame_step identifies the next step:
 * length on 0/2/4/6, CH1 sweep on 2/6, envelopes on 7, then wrap modulo eight. */
void apu_frame_clock(APU *apu);
/* Accumulate one T-cycle of stereo output and emit PCM when the rational
 * sample-rate accumulator crosses its threshold. Does not tick oscillators. */
void apu_mix_tick(APU *apu);
#endif
