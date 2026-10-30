#include "serial.h"

uint8_t serial_read(const Serial *serial, uint16_t address)
{
    return address == 0xFF01 ? serial->sb : (uint8_t)(serial->sc | 0x7Eu);
}

void serial_write(Serial *serial, uint16_t address, uint8_t value)
{
    if (address == 0xFF01) { serial->sb = value; return; }
    serial->sc = value & 0x81u; /* DMG unused bits, including bit 1, read as one. */
    serial->bits = 0;
    serial->remaining = 512;
    serial->outgoing = serial->sb;
}

bool serial_tick(Serial *serial, uint8_t *interrupt_flags, unsigned t_cycles)
{
    /* External clock with no peer must wait indefinitely. Cancellation never
     * generates an interrupt. All timing is supplied by the shared bus clock. */
    if ((serial->sc & 0x81u) != 0x81u) return false;
    while (t_cycles >= serial->remaining) {
        t_cycles -= serial->remaining;
        serial->remaining = 512;
        serial->sb = (uint8_t)(((unsigned)serial->sb << 1) | 1u);
        if (++serial->bits == 8) {
            serial->sc &= 0x01u;
            *interrupt_flags |= 0x08u;
            return true;
        }
    }
    serial->remaining -= t_cycles;
    return false;
}
