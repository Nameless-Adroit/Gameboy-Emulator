#ifndef SERIAL_H
#define SERIAL_H
#include <stdbool.h>
#include <stdint.h>

/* Disconnected DMG link port. No cable peer or CGB fast-clock mode.
 * SB shifts in ones; SC bit 7 remains set until eight internal bit clocks.
 * Startup uses a full 512-T-cycle bit period; divider phase/races are not modeled. */
typedef struct {
    uint8_t sb, sc, outgoing;
    unsigned bits, remaining;
} Serial;

uint8_t serial_read(const Serial *serial, uint16_t address);
void serial_write(Serial *serial, uint16_t address, uint8_t value);
/* Returns true once on completion, allowing the existing diagnostic stdout
 * byte sink to observe outgoing. IRQ is requested independently of IME/IE. */
bool serial_tick(Serial *serial, uint8_t *interrupt_flags, unsigned t_cycles);
#endif
