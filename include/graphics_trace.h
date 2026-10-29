#ifndef GRAPHICS_TRACE_H
#define GRAPHICS_TRACE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* Instrumentation is compiled out unless GB_ENABLE_GRAPHICS_TRACE is ON.
 * CLI parsing remains available so disabled builds can explain how to enable it. */
typedef struct {
    bool enabled;
    unsigned x, y, frames;
} GraphicsTraceOptions;

typedef struct GraphicsTrace {
    FILE *output;                 // Borrowed stream; caller owns its lifetime.
    unsigned x, y, frame_limit;
    uint64_t cycles, event_cycle; // Same elapsed T-cycles as memory_tick, no host time.
    uint64_t frame, completed_frame;
    unsigned events, rom_reads, writes, registers;
    uint16_t pc;                 // Instruction-start PC, not post-fetch PC.
    bool cpu_active, fetching, started;
} GraphicsTrace;

struct Memory;
void graphics_trace_options_init(GraphicsTraceOptions *options);
/* 1: consumed an option, 0: unrecognized, -1: invalid. Updates argv index. */
int graphics_trace_option(GraphicsTraceOptions *options, int argc, char **argv, int *index);
bool graphics_trace_start(GraphicsTrace *trace, struct Memory *memory,
                          const GraphicsTraceOptions *options, FILE *output);

#ifdef GB_GRAPHICS_TRACE
void graphics_trace_rom(GraphicsTrace *trace, uint16_t address, uint8_t value);
void graphics_trace_write(GraphicsTrace *trace, const uint8_t *vram,
                          uint8_t lcdc, uint8_t scx, uint8_t scy,
                          uint16_t address, uint8_t value);
void graphics_trace_frame_start(GraphicsTrace *trace);
void graphics_trace_vblank(GraphicsTrace *trace);
void graphics_trace_pixel(GraphicsTrace *trace, unsigned map_address, unsigned tile,
                          unsigned data_address, unsigned low, unsigned high,
                          unsigned color, unsigned bgp, unsigned shade, bool window);
void graphics_trace_sprite_pixel(GraphicsTrace *trace, unsigned index, unsigned tile,
    unsigned flags, unsigned address, unsigned low, unsigned high,
    unsigned color, unsigned obp, unsigned shade);
void graphics_trace_blank(GraphicsTrace *trace, const char *reason);
void graphics_trace_present(GraphicsTrace *trace, unsigned shade, uint32_t argb);
#endif
#endif
