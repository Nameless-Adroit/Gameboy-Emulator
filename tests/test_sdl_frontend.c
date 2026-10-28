#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sdl_frontend.h"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "line %d: %s (%s)\n", __LINE__, #x, SDL_GetError()); exit(1); } } while (0)

int main(void)
{
    SDLFrontend frontend;
    PPU ppu;
    ppu_init(&ppu);
    CHECK(sdl_frontend_init(&frontend));
    CHECK(frontend.initialized && !frontend.has_frame);
    CHECK(SDL_RenderGetIntegerScale(frontend.renderer) == SDL_TRUE);
    SDL_ScaleMode scale;
    CHECK(SDL_GetTextureScaleMode(frontend.texture, &scale) == 0);
    CHECK(scale == SDL_ScaleModeNearest);
    int w, h;
    SDL_RenderGetLogicalSize(frontend.renderer, &w, &h);
    CHECK(w == LCD_WIDTH && h == LCD_HEIGHT);

    for (unsigned y = 0; y < LCD_HEIGHT; ++y)
        for (unsigned x = 0; x < LCD_WIDTH; ++x)
            ppu.framebuffer[y][x] = (uint8_t)(x % 4);
    ppu.dot = 123; ppu.ly = 7; ppu.frame_ready = true;
    ppu.pending_interrupts = 2;
    CHECK(sdl_frontend_present(&frontend, &ppu));
    CHECK(frontend.has_frame);
    CHECK(ppu.dot == 123 && ppu.ly == 7 && ppu.frame_ready && ppu.pending_interrupts == 2);
    CHECK(frontend.pixels[0][0] == 0xFFFFFFFFu);
    CHECK(frontend.pixels[0][1] == 0xFFAAAAAAu);
    CHECK(frontend.pixels[0][2] == 0xFF555555u);
    CHECK(frontend.pixels[0][3] == 0xFF000000u);

    // Read pixels after drawing the real SDL texture, before Present invalidates
    // the backbuffer. Under the dummy/software driver the 3x output is exact.
    CHECK(SDL_RenderCopy(frontend.renderer, frontend.texture, NULL, NULL) == 0);
    Uint32 pixels[LCD_WIDTH * 3];
    SDL_Rect row = {0, 0, LCD_WIDTH * 3, 1};
    CHECK(SDL_RenderReadPixels(frontend.renderer, &row, SDL_PIXELFORMAT_ARGB8888,
                              pixels, (int)sizeof(pixels)) == 0);
    for (unsigned x = 0; x < LCD_WIDTH * 3; ++x)
        CHECK(pixels[x] == frontend.pixels[0][x / 3]);

    // Presentation caches a complete texture; subsequent PPU writes must not
    // leak into redraws triggered by host expose/resize events.
    ppu.framebuffer[0][0] = 3;
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_WINDOWEVENT;
    event.window.event = SDL_WINDOWEVENT_EXPOSED;
    CHECK(SDL_PushEvent(&event) == 1);
    bool running = true;
    CHECK(sdl_frontend_poll(&frontend, &running) && running);
    CHECK(frontend.pixels[0][0] == 0xFFFFFFFFu);

    SDL_zero(event); event.type = SDL_KEYDOWN; event.key.keysym.sym = SDLK_ESCAPE;
    CHECK(SDL_PushEvent(&event) == 1);
    CHECK(sdl_frontend_poll(&frontend, &running) && !running);
    running = true;
    SDL_zero(event); event.type = SDL_QUIT;
    CHECK(SDL_PushEvent(&event) == 1);
    CHECK(sdl_frontend_poll(&frontend, &running) && !running);
    running = true;
    SDL_zero(event); event.type = SDL_WINDOWEVENT; event.window.event = SDL_WINDOWEVENT_CLOSE;
    CHECK(SDL_PushEvent(&event) == 1);
    CHECK(sdl_frontend_poll(&frontend, &running) && !running);
    sdl_frontend_destroy(&frontend);
    CHECK(!frontend.initialized && !frontend.window && !frontend.renderer && !frontend.texture);
    sdl_frontend_destroy(&frontend); // Safe after an earlier cleanup.
    puts("PASS: SDL setup, shade conversion, nearest scaling, cached redraw, events and cleanup");
    return 0;
}
