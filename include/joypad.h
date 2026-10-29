#ifndef JOYPAD_H
#define JOYPAD_H

#include <stdbool.h>
#include <stdint.h>

/* Positive physical button mask. FF00 converts these levels to active-low
 * matrix lines. No SDL types, timing calls or CPU pointers belong here. */
typedef enum {
    JOYPAD_RIGHT  = 0x01,
    JOYPAD_LEFT   = 0x02,
    JOYPAD_UP     = 0x04,
    JOYPAD_DOWN   = 0x08,
    JOYPAD_A      = 0x10,
    JOYPAD_B      = 0x20,
    JOYPAD_SELECT = 0x40,
    JOYPAD_START  = 0x80
} JoypadButton;

typedef struct {
    uint8_t select;     // Writable FF00 bits 4/5; zero selects a row.
    uint8_t pressed;    // Physical levels, independent of row selection.
    bool wake_pending; // Selected input falling edge; consumed by cpu_step.
} Joypad;

void joypad_init(Joypad *joypad);
uint8_t joypad_read(const Joypad *joypad);
void joypad_write(Joypad *joypad, uint8_t value, uint8_t *interrupt_flags);
void joypad_set_button(Joypad *joypad, JoypadButton button, bool pressed,
                        uint8_t *interrupt_flags);
void joypad_set_buttons(Joypad *joypad, uint8_t pressed, uint8_t *interrupt_flags);

#endif
