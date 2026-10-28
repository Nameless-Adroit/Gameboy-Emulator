#include "sdl_frontend.h"
#include "graphics_trace.h"

#include <stdio.h>
#include <string.h>

static bool report_error(const char *operation)
{
    fprintf(stderr, "%s: %s\n", operation, SDL_GetError());
    return false;
}

static bool redraw(SDLFrontend *frontend)
{
    // Clear letterbox space, then let SDL scale our 160x144 texture. The cached
    // texture remains stable even while the PPU draws the next frame in RAM.
    if (SDL_SetRenderDrawColor(frontend->renderer, 255, 255, 255, 255) != 0 ||
        SDL_RenderClear(frontend->renderer) != 0)
        return report_error("Clear display");
    if (frontend->has_frame &&
        SDL_RenderCopy(frontend->renderer, frontend->texture, NULL, NULL) != 0)
        return report_error("Draw framebuffer");
    SDL_RenderPresent(frontend->renderer);
    return true;
}

void sdl_frontend_destroy(SDLFrontend *frontend)
{
    // Destroy in reverse ownership order. NULL handles are permitted here so
    // startup errors and normal shutdown share the same cleanup path.
    if (frontend->texture) SDL_DestroyTexture(frontend->texture);
    if (frontend->renderer) SDL_DestroyRenderer(frontend->renderer);
    if (frontend->window) SDL_DestroyWindow(frontend->window);
    if (frontend->initialized) SDL_Quit();
    memset(frontend, 0, sizeof(*frontend));
}

bool sdl_frontend_init(SDLFrontend *frontend)
{
    memset(frontend, 0, sizeof(*frontend));
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        report_error("Initialize SDL");
        SDL_Quit();
        return false;
    }
    frontend->initialized = true;

    // Override filtering preferences for pixel art. SetTextureScaleMode below
    // also explicitly applies nearest-neighbor filtering to this texture.
    SDL_SetHintWithPriority(SDL_HINT_RENDER_SCALE_QUALITY, "0", SDL_HINT_OVERRIDE);
    frontend->window = SDL_CreateWindow("Game Boy DMG", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, LCD_WIDTH * 3, LCD_HEIGHT * 3, SDL_WINDOW_RESIZABLE);
    if (!frontend->window) {
        report_error("Create window");
        goto fail;
    }
    SDL_SetWindowMinimumSize(frontend->window, LCD_WIDTH, LCD_HEIGHT);

    // No vsync dependency: the runner paces against emulated T-cycles instead
    // of letting the monitor refresh rate become a simulated hardware clock.
    frontend->renderer = SDL_CreateRenderer(frontend->window, -1, SDL_RENDERER_ACCELERATED);
    if (!frontend->renderer)
        frontend->renderer = SDL_CreateRenderer(frontend->window, -1, SDL_RENDERER_SOFTWARE);
    if (!frontend->renderer) {
        report_error("Create renderer");
        goto fail;
    }
    // Integer scaling gives uniformly sized pixels; SDL centers the image with
    // letterboxing when the user resizes to a nonmatching window size.
    if (SDL_RenderSetLogicalSize(frontend->renderer, LCD_WIDTH, LCD_HEIGHT) != 0 ||
        SDL_RenderSetIntegerScale(frontend->renderer, SDL_TRUE) != 0) {
        report_error("Configure display scaling");
        goto fail;
    }
    frontend->texture = SDL_CreateTexture(frontend->renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, LCD_WIDTH, LCD_HEIGHT);
    if (!frontend->texture) {
        report_error("Create framebuffer texture");
        goto fail;
    }
    if (SDL_SetTextureScaleMode(frontend->texture, SDL_ScaleModeNearest) != 0 ||
        SDL_SetTextureBlendMode(frontend->texture, SDL_BLENDMODE_NONE) != 0) {
        report_error("Configure framebuffer texture");
        goto fail;
    }
    if (!redraw(frontend)) goto fail; // Initial white display before first VBlank.
    return true;
fail:
    sdl_frontend_destroy(frontend);
    return false;
}

bool sdl_frontend_poll(SDLFrontend *frontend, bool *running)
{
    SDL_Event event;
    bool needs_redraw = false;
    // Poll on the main/video thread. Do this even without frame_ready so a ROM
    // that disables its LCD or enters STOP cannot leave an unresponsive window.
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT ||
            (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) ||
            (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE))
            *running = false;
        if (event.type == SDL_WINDOWEVENT &&
            (event.window.event == SDL_WINDOWEVENT_EXPOSED ||
             event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED))
            needs_redraw = true;
    }
    return !*running || !needs_redraw || redraw(frontend);
}

bool sdl_frontend_present(SDLFrontend *frontend, const PPU *ppu)
{
    // Explicit integer ARGB values match SDL_PIXELFORMAT_ARGB8888 on either
    // endian host. PPU memory stays in portable DMG shade IDs, not SDL colors.
    static const Uint32 shades[4] = {
        0xFFFFFFFFu, 0xFFAAAAAAu, 0xFF555555u, 0xFF000000u
    };
    for (unsigned y = 0; y < LCD_HEIGHT; ++y)
        for (unsigned x = 0; x < LCD_WIDTH; ++x)
            frontend->pixels[y][x] = shades[ppu->framebuffer[y][x] & 3u];
    if (SDL_UpdateTexture(frontend->texture, NULL, frontend->pixels,
                          (int)sizeof(frontend->pixels[0])) != 0)
        return report_error("Upload framebuffer");
    frontend->has_frame = true;
    if (!redraw(frontend)) return false;
#ifdef GB_GRAPHICS_TRACE
    if (ppu->trace)
        graphics_trace_present(ppu->trace, ppu->framebuffer[ppu->trace->y][ppu->trace->x],
            frontend->pixels[ppu->trace->y][ppu->trace->x]);
#endif
    return true;
}
