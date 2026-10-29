#ifndef SDL_FRONTEND_H
#define SDL_FRONTEND_H

#include <stdbool.h>
#include <SDL.h>
#include "ppu.h"
#include "joypad.h"

/* This entire type belongs to the host presentation layer. No SDL types or
 * callbacks are added to CPU, Memory, or PPU. One instance owns one window. */
typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    Uint32 pixels[LCD_HEIGHT][LCD_WIDTH];
    bool initialized;
    bool has_frame;
    Joypad *joypad;              // Borrowed input sink; NULL for display-only use.
    uint8_t *joypad_interrupts;  // Borrowed IF byte, not a CPU bus write.
    bool input_focused;
} SDLFrontend;

/* Initialize before use. On any partial failure this cleans up its own resources. */
bool sdl_frontend_init(SDLFrontend *frontend);
void sdl_frontend_destroy(SDLFrontend *frontend);

// Bind after init. SDL owns the keyboard button levels; events apply immediately
// without ticking the core. Both pointers must outlive the frontend, or be NULL.
void sdl_frontend_bind_joypad(SDLFrontend *frontend, Joypad *joypad, uint8_t *interrupt_flags);

/* Drain host events; closing the window or pressing Escape clears *running.
 * Expose/resize events redraw the last uploaded frame, never an in-progress one. */
bool sdl_frontend_poll(SDLFrontend *frontend, bool *running);

/* Convert DMG shade IDs to host pixels and upload one completed image. This is
 * read-only access to the PPU: acknowledging frame_ready belongs to the runner. */
bool sdl_frontend_present(SDLFrontend *frontend, const PPU *ppu);

#endif
