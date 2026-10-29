#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu.h"
#include "graphics_trace.h"
#include "sdl_frontend.h"
#include "sdl_audio.h"

/* Host-only pacing: cpu_step remains the sole driver of emulated hardware time.
 * Delays reduce CPU usage when ahead. When behind, run normally without skipping
 * instructions, PPU dots or frames. Cap sleeps to keep quit events responsive. */
static void pace(uint64_t cycles, Uint64 started, Uint64 frequency)
{
    double target_ms = (double)cycles * 1000.0 / 4194304.0;
    double elapsed_ms = (double)(SDL_GetPerformanceCounter() - started) * 1000.0 /
                        (double)frequency;
    double ahead_ms = target_ms - elapsed_ms;
    if (ahead_ms >= 1.0) SDL_Delay((Uint32)(ahead_ms > 4.0 ? 4.0 : ahead_ms));
}

int main(int argc, char **argv)
{
    unsigned long frame_limit = 0;
    bool muted = false;
    GraphicsTraceOptions trace_options;
    graphics_trace_options_init(&trace_options);
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <rom.gb> [--frames N] [--mute] [--graphics-trace] [--trace-pixel X,Y] [--trace-frames N]\n", argv[0]);
        return EXIT_FAILURE;
    }
    for (int i = 2; i < argc; ++i) {
        int handled = graphics_trace_option(&trace_options, argc, argv, &i);
        if (handled < 0) return EXIT_FAILURE;
        if (handled) continue;
        if (strcmp(argv[i], "--mute") == 0) { muted = true; continue; }
        if (strcmp(argv[i], "--frames") != 0 || i + 1 >= argc) {
            fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]);
            return EXIT_FAILURE;
        }
        char *end;
        const char *text = argv[++i];
        errno = 0;
        frame_limit = strtoul(text, &end, 10);
        if (errno || *end || text[0] < '0' || text[0] > '9' || frame_limit == 0) {
            fprintf(stderr, "Invalid frame limit: %s\n", text);
            return EXIT_FAILURE;
        }
    }
    GraphicsTrace trace;
    CPU cpu;
    Memory memory;
    SDLFrontend frontend;
    SDLAudio audio = {0};
    memory_init(&memory);
    // Load before opening a window so bad paths/cartridges fail cleanly without
    // initializing the video subsystem or leaving partially created resources.
    if (!load_rom(&memory.cartridge, argv[1])) return EXIT_FAILURE;
    reset_cpu(&cpu, &memory);
    if (!graphics_trace_start(&trace, &memory, &trace_options, stderr)) return EXIT_FAILURE;
    if (!sdl_frontend_init(&frontend)) return EXIT_FAILURE;
    sdl_frontend_bind_joypad(&frontend, &memory.joypad, &memory.io[0x0F]);
    if (!muted) (void)sdl_audio_init(&audio);

    bool running = true;
    int result = EXIT_SUCCESS;
    unsigned long frames = 0;
    uint64_t cycle_origin = cpu.total_cycles;
    Uint64 started = SDL_GetPerformanceCounter();
    Uint64 frequency = SDL_GetPerformanceFrequency();
    while (running) {
        if (!sdl_frontend_poll(&frontend, &running)) {
            result = EXIT_FAILURE;
            break;
        }
        if (cpu.state == CPU_STOPPED && memory.joypad.wake_pending) {
            // Host-only pacing restarts after a paused STOP session. Do not run
            // a burst of emulation to catch up with wall time spent waiting.
            started = SDL_GetPerformanceCounter();
            cycle_origin = cpu.total_cycles;
        }
        // Bound CPU work between event polls independently of VBlank. Checking
        // frame_ready after every step prevents stepping into next-frame drawing
        // before uploading the completed image.
        for (unsigned step = 0; running && step < 256; ++step) {
            if (cpu.state == CPU_LOCKED) {
                fprintf(stderr, "CPU locked at PC=%04X\n", cpu.PC);
                result = EXIT_FAILURE;
                running = false;
                break;
            }
            if (cpu.state == CPU_STOPPED && !memory.joypad.wake_pending) {
                if (frame_limit) {
                    fprintf(stderr, "CPU stopped before reaching frame limit\n");
                    result = EXIT_FAILURE;
                    running = false;
                }
                break; // Normal sessions keep polling keyboard input and host quit events.
            }
            cpu_step(&cpu, &memory); // Already clocks all peripherals; never tick again.
            if (memory.ppu.frame_ready) {
                if (!sdl_frontend_present(&frontend, &memory.ppu)) {
                    result = EXIT_FAILURE;
                    running = false;
                    break;
                }
                // Acknowledge only after successful presentation. This modifies
                // the consumer latch, never the PPU's dot/LY/interrupt state.
                memory.ppu.frame_ready = false;
                if (frames != ULONG_MAX) ++frames;
                if (frame_limit && frames >= frame_limit) running = false;
            }
        }
        if (!sdl_audio_pump(&audio, &memory.apu)) { result = EXIT_FAILURE; break; }
        if (cpu.state == CPU_STOPPED) { sdl_audio_clear(&audio); SDL_Delay(1); }
        else pace(cpu.total_cycles - cycle_origin, started, frequency);
    }
    if (frame_limit && result == EXIT_SUCCESS)
        printf("Presented %lu frame(s)\n", frames);
    sdl_audio_destroy(&audio);
    sdl_frontend_destroy(&frontend);
    return result;
}
