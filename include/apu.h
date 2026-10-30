#ifndef APU_H
#define APU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Fixed DMG clock, host PCM rate, and ring capacity in stereo frames. */
#define APU_CLOCK_HZ 4194304u
#define APU_SAMPLE_RATE 48000u
#define APU_BUFFER_FRAMES 4096u

/* Channel state contains no host resources. Register bytes remain owned by APU. */
typedef struct {
    /* Generator activity, DAC power, and automatic length-expiry control are
     * independent: an inactive generator may still have its DAC powered. */
    bool enabled, dac, length_enabled;
    /* length counts sequencer length ticks; timer counts CPU T-cycles.
     * position indexes eight pulse duty steps or 32 wave nibbles. */
    unsigned length, timer, position;
    /* Current 0..15 amplitude and countdown in 64 Hz envelope ticks.
     * These are runtime values, separate from the initial-volume register. */
    unsigned volume, envelope_timer;
    bool envelope_running;
    /* CH1 only: private frequency copy and countdown in 128 Hz sweep ticks.
     * sweep_negated records a subtraction for the NR10 negate-clear rule. */
    unsigned sweep_shadow, sweep_timer;
    bool sweep_enabled, sweep_negated;
    uint16_t lfsr;       /* CH4 noise state; low bit selects inverted output. */
    uint8_t wave_sample; /* CH3's latched nibble, retained across retriggers. */
    unsigned wave_access_cycles; /* CH3: remaining T-cycles of CPU RAM access. */
} APUChannel;

typedef struct {
    bool powered; /* NR52 master power; independent of host mute/playback. */
    uint8_t regs[0x17]; /* FF10..FF26, including unused holes. */
    uint8_t wave[16]; /* FF30..FF3F: two four-bit samples per byte. */
    APUChannel channels[4];
    unsigned frame_step; /* Next 512 Hz sequencer step, 0..7. */
    /* Phase adds APU_SAMPLE_RATE per T-cycle and wraps at APU_CLOCK_HZ.
     * accumulated_cycles is the divisor for the current box-average interval. */
    unsigned sample_phase, accumulated_cycles;
    /* Per-side signal sums and persistent approximate high-pass filter state;
     * index 0 is left, index 1 right throughout mixing and PCM storage. */
    double accumulated[2], capacitor[2];
    int16_t samples[APU_BUFFER_FRAMES][2]; /* interleaved native-endian L/R */
    size_t read_index, buffered; /* Oldest ring slot and unread stereo frames. */
    uint64_t dropped_frames; /* Oldest frames discarded when consumer falls behind. */
} APU;

/* Core APIs are used on the emulation thread; no allocation or SDL ownership.
 * Initialization creates powered-off state, not boot-ROM register defaults. */
void apu_init(APU *apu);
/* Untimed bus handlers: reads apply masks/status, writes perform immediate
 * register side effects. Out-of-range/unused reads return FF; writes ignore. */
uint8_t apu_read(const APU *apu, uint16_t address);
void apu_write(APU *apu, uint16_t address, uint8_t value);
/* Advance exactly the elapsed cycles supplied by memory_tick(). divider_before
 * is Timer's existing 16-bit system divider at the start of that interval;
 * the local traversal detects edges without modifying Timer or ticking CPU. */
void apu_tick(APU *apu, unsigned cycles, uint16_t divider_before);
/* Notify FF04's untimed reset: a previously high divider bit 12 falls to zero
 * and clocks the sequencer once. Caller performs the actual timer reset. */
void apu_div_reset(APU *apu, uint16_t divider_before);
/* Drain up to capacity stereo frames into 2 * capacity int16_t elements,
 * ordered L,R,L,R. Returns frames copied, not individual samples or bytes.
 * Consumes only the PCM queue; no simulated time or registers are advanced. */
size_t apu_read_samples(APU *apu, int16_t *interleaved, size_t capacity);

#endif
