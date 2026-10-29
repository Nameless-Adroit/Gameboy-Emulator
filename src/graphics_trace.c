#include "graphics_trace.h"
#include "memory.h"
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

void graphics_trace_options_init(GraphicsTraceOptions *options)
{
    *options = (GraphicsTraceOptions){false, 20, 40, 2};
}

static bool number(const char *text, unsigned max, unsigned *value, const char **end)
{
    char *tail;
    errno = 0;
    unsigned long n = strtoul(text, &tail, 10);
    if (errno || text[0] < '0' || text[0] > '9' || n > max) return false;
    *value = (unsigned)n;
    *end = tail;
    return true;
}

int graphics_trace_option(GraphicsTraceOptions *options, int argc, char **argv, int *index)
{
    const char *arg = argv[*index], *end;
    if (strcmp(arg, "--graphics-trace") == 0) {
        options->enabled = true;
        return 1;
    }
    if (strcmp(arg, "--trace-pixel") == 0) {
        if (*index + 1 >= argc) goto invalid;
        const char *text = argv[++*index];
        if (!number(text, LCD_WIDTH - 1, &options->x, &end) || *end != ',' ||
            !number(end + 1, LCD_HEIGHT - 1, &options->y, &end) || *end) goto invalid;
    } else if (strcmp(arg, "--trace-frames") == 0) {
        if (*index + 1 >= argc) goto invalid;
        if (!number(argv[++*index], 60, &options->frames, &end) || *end ||
            options->frames == 0) goto invalid;
    } else return 0;
    options->enabled = true;
    return 1;
invalid:
    fprintf(stderr, "Invalid %s (pixel: 0..159,0..143; frames: 1..60)\n", arg);
    return -1;
}

bool graphics_trace_start(GraphicsTrace *trace, Memory *memory,
                          const GraphicsTraceOptions *options, FILE *output)
{
#ifdef GB_GRAPHICS_TRACE
    // Detach before reinitializing observer state, including the disabled case.
    memory->trace = NULL;
    memory->ppu.trace = NULL;
#endif
    memset(trace, 0, sizeof(*trace));
    if (!options->enabled) return true;
#ifndef GB_GRAPHICS_TRACE
    (void)memory; (void)output;
    fprintf(stderr, "Graphics trace support is disabled; configure with -DGB_ENABLE_GRAPHICS_TRACE=ON\n");
    return false;
#else
    if (!output || options->x >= LCD_WIDTH || options->y >= LCD_HEIGHT ||
        options->frames == 0 || options->frames > 60) return false;
    trace->output = output;
    trace->x = options->x; trace->y = options->y;
    trace->frame_limit = options->frames; trace->frame = 1;
    memory->trace = trace;
    memory->ppu.trace = trace;
    fprintf(output, "[gfx frame=1 cycle=0] START pixel=(%u,%u) frames=%u event_cap=256; attach before execution\n",
            trace->x, trace->y, trace->frame_limit);
    return true;
#endif
}

#ifdef GB_GRAPHICS_TRACE
/* Independent category quotas keep ROM copies from drowning out the selected
 * pixel. The global limit includes one final LIMIT record and then stays silent. */
static void event(GraphicsTrace *t, uint64_t frame, uint64_t cycle, const char *fmt, ...)
{
    if (frame > t->frame_limit || t->events >= 256) return;
    ++t->events;
    fprintf(t->output, "[gfx frame=%" PRIu64 " cycle=%" PRIu64 "] ", frame, cycle);
    if (t->events == 256) {
        fputs("LIMIT event_cap=256; further trace events suppressed\n", t->output);
        return;
    }
    va_list args;
    va_start(args, fmt);
    vfprintf(t->output, fmt, args);
    va_end(args);
    fputc('\n', t->output);
}

void graphics_trace_rom(GraphicsTrace *t, uint16_t address, uint8_t value)
{
    if (t->fetching || t->rom_reads >= 16) return;
    ++t->rom_reads;
    if (t->cpu_active)
        event(t, t->frame, t->cycles, "ROM_DATA_READ pc=%04X address=%04X value=%02X via=memory_read/cartridge_read", t->pc, address, value);
    else
        event(t, t->frame, t->cycles, "ROM_DATA_READ pc=unknown origin=raw-bus address=%04X value=%02X", address, value);
}

void graphics_trace_write(GraphicsTrace *t, const uint8_t *vram,
                          uint8_t lcdc, uint8_t scx, uint8_t scy,
                          uint16_t address, uint8_t value)
{
    bool reg = address >= 0xFF40 && address <= 0xFF4B && address != 0xFF46;
    bool video = address >= 0x8000 && address <= 0x9FFF;
    if (!reg && !video) return;
    if (reg) {
        if (t->registers++ >= 32) return;
    } else {
        unsigned bx = (t->x + scx) & 255u, by = (t->y + scy) & 255u;
        unsigned map = ((lcdc & 8) ? 0x1C00u : 0x1800u) + by / 8 * 32 + bx / 8;
        unsigned id = vram[map];
        unsigned row = (by & 7u) * 2;
        int signed_id = id < 128 ? (int)id : (int)id - 256;
        unsigned unsigned_row = 0x8000 + id * 16 + row;
        unsigned signed_row = (unsigned)(0x9000 + signed_id * 16) + row;
        /* A small prefix illustrates a ROM copy. Beyond that, watch the chosen
         * map entry and both candidate tile-row locations (LCDC may change). */
        bool watched = address == 0x8000 + map || address == unsigned_row ||
            address == unsigned_row + 1 || address == signed_row || address == signed_row + 1;
        if (t->writes >= 64 || (t->writes >= 16 && !watched)) return;
        ++t->writes;
    }
    if (t->cpu_active)
        event(t, t->frame, t->cycles, "%s pc=%04X address=%04X value=%02X", reg ? "PPU_REGISTER_WRITE" : "VRAM_WRITE", t->pc, address, value);
    else
        event(t, t->frame, t->cycles, "%s pc=unknown origin=raw-bus address=%04X value=%02X", reg ? "PPU_REGISTER_WRITE" : "VRAM_WRITE", address, value);
}

void graphics_trace_frame_start(GraphicsTrace *t)
{
    if (t->started) ++t->frame;
    t->started = true;
}

void graphics_trace_vblank(GraphicsTrace *t)
{
    t->completed_frame = t->frame;
    event(t, t->frame, t->event_cycle, "VBLANK frame_ready=1");
}

void graphics_trace_pixel(GraphicsTrace *t, unsigned map_address, unsigned tile,
                          unsigned data_address, unsigned low, unsigned high,
                          unsigned color, unsigned bgp, unsigned shade, bool window)
{
    event(t, t->frame, t->event_cycle,
        "PIXEL xy=%u,%u map=%04X tile=%02X row_address=%04X bytes=%02X,%02X color=%u BGP=%02X palette=%u framebuffer=%u layer=%s",
        t->x, t->y, map_address, tile, data_address, low, high, color, bgp, shade, shade,
        window ? "window" : "background");
}

void graphics_trace_blank(GraphicsTrace *t, const char *reason)
{
    event(t, t->frame, t->event_cycle, "PIXEL xy=%u,%u source=%s map=n/a tile=n/a row_address=n/a bytes=n/a color=n/a palette=n/a framebuffer=0", t->x, t->y, reason);
}

void graphics_trace_present(GraphicsTrace *t, unsigned shade, uint32_t argb)
{
    if (!t->completed_frame) return; // No completed PPU pass to correlate yet.
    event(t, t->completed_frame, t->cycles, "SDL_PRESENT xy=%u,%u framebuffer=%u SDL_ARGB=%08" PRIX32 " via=SDL_UpdateTexture/SDL_RenderCopy/SDL_RenderPresent", t->x, t->y, shade, argb);
}
#endif
