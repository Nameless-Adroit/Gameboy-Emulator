#include "joypad.h"

static uint8_t input_lines(const Joypad *joypad)
{
    unsigned lines = 0x0F;
    if (!(joypad->select & 0x10)) lines &= ~(unsigned)joypad->pressed;
    if (!(joypad->select & 0x20)) lines &= ~((unsigned)joypad->pressed >> 4);
    return (uint8_t)(lines & 0x0F);
}

static void request_edge(Joypad *joypad, uint8_t previous, uint8_t *interrupt_flags)
{
    // Multiple selected buttons can share the same low line. Pressing another
    // button on an already-low line does not create a second falling edge.
    if ((previous & (uint8_t)~input_lines(joypad) & 0x0F) != 0) {
        *interrupt_flags |= 0x10;
        joypad->wake_pending = true;
    }
}

void joypad_init(Joypad *joypad)
{
    *joypad = (Joypad){0x30, 0, false}; // No row selected, all buttons released.
}

uint8_t joypad_read(const Joypad *joypad)
{
    return (uint8_t)(0xC0 | joypad->select | input_lines(joypad));
}

void joypad_write(Joypad *joypad, uint8_t value, uint8_t *interrupt_flags)
{
    uint8_t previous = input_lines(joypad);
    joypad->select = value & 0x30; // Upper bits read high; lower lines are read-only.
    request_edge(joypad, previous, interrupt_flags);
}

void joypad_set_buttons(Joypad *joypad, uint8_t pressed, uint8_t *interrupt_flags)
{
    uint8_t previous = input_lines(joypad);
    joypad->pressed = pressed;
    request_edge(joypad, previous, interrupt_flags);
}

void joypad_set_button(Joypad *joypad, JoypadButton button, bool pressed,
                        uint8_t *interrupt_flags)
{
    uint8_t mask = (uint8_t)button;
    uint8_t state = pressed ? (uint8_t)(joypad->pressed | mask)
                            : (uint8_t)(joypad->pressed & (uint8_t)~mask);
    joypad_set_buttons(joypad, state, interrupt_flags);
}
