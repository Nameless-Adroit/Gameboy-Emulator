
#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    // Internal system counter: advances once per T-cycle and wraps at 65536.
    // DIV (FF04) exposes its upper 8 bits; TIMA uses selected falling edges.
    uint16_t divider;

    uint8_t tima; // TIMA register, readable/writable at 0xFF05.
    uint8_t tma; // TMA register, readable/writable at 0xFF06.
    uint8_t tac; // TAC register, readable/writable at 0xFF07.

    // Counts down the delay after TIMA overflows.
    uint8_t reload_delay; // When non-zero, TIMA will be reloaded with TMA after this many cycles.
} Timer;

void timer_tick(Timer *timer,
                uint8_t *interrupt_flags,
                unsigned t_cycles);

uint8_t timer_read(const Timer *timer,
                   uint16_t address);

void timer_write(Timer *timer,
                 uint16_t address,
                 uint8_t value);

#endif
