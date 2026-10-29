#include <stdio.h>
#include <stdlib.h>
#include "cpu.h"
#include "sdl_frontend.h"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "line %d: %s (%s)\n", __LINE__, #x, SDL_GetError()); exit(1); } } while (0)

static void key(SDL_Scancode code, bool down, bool repeat)
{
    SDL_Event e;
    SDL_zero(e); e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.keysym.scancode = code; e.key.repeat = repeat ? 1 : 0;
    CHECK(SDL_PushEvent(&e) == 1);
}

static void window_event(Uint8 kind)
{
    SDL_Event e;
    SDL_zero(e); e.type = SDL_WINDOWEVENT; e.window.event = kind;
    CHECK(SDL_PushEvent(&e) == 1);
}

int main(void)
{
    Memory m;
    CPU c;
    SDLFrontend frontend;
    bool running = true;
    memory_init(&m); reset_cpu(&c, &m);
    CHECK(sdl_frontend_init(&frontend));
    sdl_frontend_bind_joypad(&frontend, &m.joypad, &m.io[0x0F]);
    CHECK(sdl_frontend_poll(&frontend, &running));
    window_event(SDL_WINDOWEVENT_FOCUS_GAINED);
    const SDL_Scancode keys[] = {SDL_SCANCODE_RIGHT,SDL_SCANCODE_LEFT,SDL_SCANCODE_UP,
        SDL_SCANCODE_DOWN,SDL_SCANCODE_Z,SDL_SCANCODE_X,SDL_SCANCODE_BACKSPACE,SDL_SCANCODE_RETURN};
    for (unsigned i = 0; i < 8; ++i) {
        unsigned select = i < 4 ? 0x20u : 0x10u;
        memory_write(&m, 0xFF00, (uint8_t)select); m.io[0x0F] = 0;
        key(keys[i], true, false); CHECK(sdl_frontend_poll(&frontend, &running) && running);
        CHECK(m.joypad.pressed == (1u << i) && m.io[0x0F] == 0x10);
        CHECK(memory_read(&m, 0xFF00) == (uint8_t)((0xCF | select) & ~(1u << (i % 4))));
        m.io[0x0F] = 0;
        key(keys[i], true, true); key(keys[i], true, false);
        CHECK(sdl_frontend_poll(&frontend, &running) && m.io[0x0F] == 0);
        key(keys[i], false, false); CHECK(sdl_frontend_poll(&frontend, &running));
        CHECK(m.joypad.pressed == 0 && m.io[0x0F] == 0);
    }
    CHECK(c.total_cycles == 0 && m.timer.divider == 0 && m.ppu.dot == 0);
    memory_write(&m, 0xFF00, 0); m.io[0x0F] = 0;
    key(SDL_SCANCODE_Z, true, false); key(SDL_SCANCODE_Z, false, false);
    CHECK(sdl_frontend_poll(&frontend, &running));
    CHECK(m.joypad.pressed == 0 && m.io[0x0F] == 0x10); // Preserve quick tap's edge.
    key(SDL_SCANCODE_Z, true, false); CHECK(sdl_frontend_poll(&frontend, &running));
    m.io[0x0F] = 0;
    key(SDL_SCANCODE_RIGHT, true, false); key(SDL_SCANCODE_Z, false, false);
    CHECK(sdl_frontend_poll(&frontend, &running));
    CHECK(m.joypad.pressed == JOYPAD_RIGHT && memory_read(&m, 0xFF00) == 0xCE && m.io[0x0F] == 0);
    window_event(SDL_WINDOWEVENT_FOCUS_LOST); CHECK(sdl_frontend_poll(&frontend, &running));
    CHECK(m.joypad.pressed == 0 && m.io[0x0F] == 0 && !frontend.input_focused);
    key(SDL_SCANCODE_X, true, false); CHECK(sdl_frontend_poll(&frontend, &running));
    CHECK(m.joypad.pressed == 0); // Ignore presses while unfocused.
    window_event(SDL_WINDOWEVENT_FOCUS_GAINED); key(SDL_SCANCODE_X, true, false);
    CHECK(sdl_frontend_poll(&frontend, &running) && m.joypad.pressed == JOYPAD_B);
    window_event(SDL_WINDOWEVENT_MINIMIZED); CHECK(sdl_frontend_poll(&frontend, &running));
    CHECK(m.joypad.pressed == 0);
    window_event(SDL_WINDOWEVENT_FOCUS_GAINED); CHECK(sdl_frontend_poll(&frontend, &running));

    // Real SDL event -> core joypad edge -> CPU STOP wake (no direct SDL CPU callback).
    joypad_set_buttons(&m.joypad, 0, &m.io[0x0F]); m.joypad.wake_pending = false;
    m.io[0x0F] = 0; memory_write(&m, 0xFF00, 0x20);
    m.cartridge.rom_loaded = true; m.cartridge.rom[0x100] = 0x10;
    CHECK(cpu_step(&c, &m) == 4 && c.state == CPU_STOPPED);
    key(SDL_SCANCODE_RIGHT, true, false); CHECK(sdl_frontend_poll(&frontend, &running));
    CHECK(c.total_cycles == 4 && m.joypad.wake_pending);
    CHECK(cpu_step(&c, &m) == 4 && c.state == CPU_RUNNING && c.PC == 0x103);
    CHECK(m.timer.divider == 8);
    sdl_frontend_bind_joypad(&frontend, NULL, NULL);
    CHECK(m.joypad.pressed == 0);
    sdl_frontend_bind_joypad(&frontend, &m.joypad, &m.io[0x0F]);
    key(SDL_SCANCODE_Z, true, false); CHECK(sdl_frontend_poll(&frontend, &running));
    CHECK(m.joypad.pressed == JOYPAD_A);
    SDL_Event quit;
    SDL_zero(quit); quit.type = SDL_QUIT; CHECK(SDL_PushEvent(&quit) == 1);
    CHECK(sdl_frontend_poll(&frontend, &running) && !running && m.joypad.pressed == 0);
    sdl_frontend_destroy(&frontend); sdl_frontend_destroy(&frontend);
    puts("PASS: SDL keyboard mapping, repeat, simultaneous keys, focus loss, tap edges, STOP wake and cleanup");
    return 0;
}
