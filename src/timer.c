#include "timer.h"

static unsigned selected_bit(uint8_t tac)
{
    switch (tac & 0x03) {
        case 0: return 9;
        case 1: return 3;
        case 2: return 5;
        default: return 7;
    }
}

static bool timer_signal(const Timer *timer)
{
    if ((timer->tac & 0x04) == 0)
        return false;

    return ((timer->divider >>
             selected_bit(timer->tac)) & 1u) != 0;
}

static void increment_tima(Timer *timer)
{
    // Ignore edges while an overflow reload is pending.
    if (timer->reload_delay != 0)
        return;

    if (timer->tima == 0xFF) {
        timer->tima = 0;
        timer->reload_delay = 4;
    } else {
        timer->tima++;
    }
}

void timer_tick(Timer *timer,
                uint8_t *interrupt_flags,
                unsigned t_cycles)
{
    for (unsigned i = 0; i < t_cycles; i++) {

        // Complete a previously scheduled reload.
        if (timer->reload_delay != 0) {
            timer->reload_delay--;

            if (timer->reload_delay == 0) {
                timer->tima = timer->tma;

                // Request timer interrupt: IF bit 2.
                *interrupt_flags |= 0x04;
            }
        }

        bool old_signal = timer_signal(timer);

        timer->divider++;

        bool new_signal = timer_signal(timer);

        // TIMA increments on a falling edge.
        if (old_signal && !new_signal)
            increment_tima(timer);
    }
}

uint8_t timer_read(const Timer *timer,
                   uint16_t address)
{
    switch (address) {
        case 0xFF04:
            return (uint8_t)(timer->divider >> 8);

        case 0xFF05:
            return timer->tima;

        case 0xFF06:
            return timer->tma;

        case 0xFF07:
            return (uint8_t)(timer->tac | 0xF8);

        default:
            return 0xFF;
    }
}

void timer_write(Timer *timer,
                 uint16_t address,
                 uint8_t value)
{
    bool old_signal = timer_signal(timer);

    switch (address) {

        case 0xFF04:
            timer->divider = 0;
            break;

        case 0xFF05:
            timer->tima = value;
            timer->reload_delay = 0;
            break;

        case 0xFF06:
            timer->tma = value;
            break;

        case 0xFF07:
            timer->tac = value & 0x07;
            break;

        default:
            return;
    }

    // DIV and TAC writes can cause a falling edge.
    bool new_signal = timer_signal(timer);

    if (old_signal && !new_signal)
        increment_tima(timer);
}
