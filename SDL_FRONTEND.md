# Minimal SDL2 frontend

`gb_sdl` presents the PPU's existing framebuffer in an Ubuntu desktop window.
The PPU creates DMG shade IDs; SDL converts those IDs to real display pixels.
The CPU, timer, memory bus and PPU do not include SDL headers or depend on SDL.
The C99 core and the original headless `gb` executable remain unchanged.

## Build and run on Ubuntu

Install SDL2 development files (version 2.0.12 or newer), CMake and a compiler:

```sh
sudo apt-get update
sudo apt-get install build-essential cmake libsdl2-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DGB_ENABLE_SDL=ON
cmake --build build -j2
./build/gb_sdl tests/diagnostic/background.gb
```

The diagnostic shows grayscale bars, checkerboard and X tiles in a 480 × 432
window (3× native resolution). Resize freely; integer nearest-neighbor scaling
keeps pixel edges crisp, with white letterboxing around nonmatching sizes.
Press **Escape** or close the window to exit. Arrow keys control the D-pad,
Z/X are A/B, Enter is Start and Backspace is Select. Focus loss releases held
buttons. See [JOYPAD.md](JOYPAD.md) for input mapping and controller behavior.
Audio remains unimplemented.

SDL is opt-in. For a build that requires no SDL development files:

```sh
cmake -S . -B build -DGB_ENABLE_SDL=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

`gb_sdl` is an additional executable; `gb` keeps its existing headless behavior.
The frontend is subject to the same ROM/mapper limits as that executable.

## Ownership and function responsibilities

| Component/function | Responsibility |
| --- | --- |
| `gbcore` | CPU execution, hardware clock, memory bus, timer, APU, PPU and framebuffer generation; never links SDL |
| `gb_sdl_frontend` | Separate SDL library target that owns a window, renderer, texture and converted host pixels |
| `src/frontend/main.c` | Loads ROM, initializes CPU/bus, polls events, steps CPU, presents completed frames and acknowledges the frame latch |
| `sdl_frontend_init()` | Initializes SDL video/timer, opens a resizable window, creates an accelerated renderer with software fallback, configures scaling and creates a streaming texture |
| `sdl_frontend_present()` | Reads the PPU framebuffer, converts shades to ARGB8888, uploads the completed image and draws it; does not mutate the PPU |
| `sdl_frontend_poll()` | Drains events, updates bound joypad levels, handles focus/Close/Escape and requests cached redraws |
| `redraw()` (private) | Clears letterboxing, copies the last uploaded texture and presents it; never reads a partially drawn PPU framebuffer |
| `sdl_frontend_bind_joypad()` | Binds the core Joypad and IF byte after initialization; input events never advance the hardware clock |
| `sdl_frontend_destroy()` | Destroys texture, renderer and window in reverse order, then shuts down SDL; also handles partial startup and repeated cleanup safely |
| `report_error()` (private) | Reports the failed SDL operation and SDL's error string |
| `pace()` (private to runner) | Uses host performance counters and bounded sleeps to limit wall-clock speed without altering simulated hardware time |

All SDL event and rendering calls stay on the main/video thread. The accelerated
renderer does not request vsync; software rendering is the fallback for systems
without acceleration and for automated tests. Presentation needs no changes to
the core's memory layout or timing APIs.

## Frame and clock flow

```text
cpu_step()
  -> existing timed CPU accesses/internal cycles
     -> memory_tick() -> timer_tick() + ppu_tick() + dma_tick()
  -> runner checks ppu.frame_ready
     -> sdl_frontend_present() uploads the completed framebuffer
     -> runner clears frame_ready only after success
```

The runner checks `frame_ready` after every CPU step, before stepping into any
subsequent frame drawing. It does not call `memory_tick()` or `ppu_tick()` itself.
SDL time measures elapsed host time only. `SDL_Delay()` sleeps without adding,
removing or replacing CPU cycles, PPU dots or interrupt requests. Pacing compares
`cpu.total_cycles / 4194304` with elapsed host seconds. When behind, the runner
keeps executing normally rather than skipping instructions or frames. This is
basic pacing, not a claim of exact real-time synchronization or audio sync.

Events are polled after at most 256 CPU steps, independently of frame readiness.
This remains responsive while LCD is disabled or the CPU is halted. HALT still
uses the normal `cpu_step()` idle path, so hardware continues ticking. STOP
leaves the last image visible while processing host events with a short sleep;
a new selected joypad input edge wakes it through cpu_step(). Host pacing resets
after that pause rather than catching up the stopped interval. A locked CPU is reported and exits with
failure. With LCD disabled, the last uploaded texture remains visible until the
next completed frame; the frontend does not manufacture new frame-ready events.

An initial white display appears before the first frame. Resize/expose redraws
use the most recently uploaded texture, so they do not display partially written
rows from the next frame. PPU frame memory is read-only to the SDL layer.

## Pixel format and scaling

Framebuffer shade IDs map to opaque grayscale ARGB values:

| Shade | Host ARGB8888 |
| --- | --- |
| 0: white | FFFFFFFF |
| 1: light gray | FFAAAAAA |
| 2: dark gray | FF555555 |
| 3: black | FF000000 |

The texture remains exactly 160 × 144; uploading it does not resize or rewrite
the PPU's framebuffer. SDL logical size is 160 × 144 and integer scaling is
explicitly enabled. The scale-quality hint and the texture's nearest scale mode
both select nearest-neighbor filtering. See the official SDL2 documentation for
[logical size](https://wiki.libsdl.org/SDL2/SDL_RenderSetLogicalSize),
[filtering](https://wiki.libsdl.org/SDL2/SDL_HINT_RENDER_SCALE_QUALITY), and
[event polling](https://wiki.libsdl.org/SDL2/SDL_PollEvent).

## Tests and startup/shutdown checks

With `GB_ENABLE_SDL=ON`, CTest adds two tests alongside the seven core suites:

- `SDL_Frontend_Tests`: creates the real SDL window/renderer/texture using the
  dummy/software driver, verifies all four shades, reads rendered output pixels
  to check exact 3× nearest scaling, verifies that presentation leaves PPU state
  unchanged, and tests cached redraw, Escape, SDL_QUIT, window-close and cleanup.
- `SDL_Diagnostic_Smoke`: runs the diagnostic ROM through `gb_sdl` until two
  complete frames have been presented, then exits normally. A timeout catches
  hangs and the expected output confirms two actual frame-ready presentations.

```sh
ctest --test-dir build --output-on-failure
# Display-server-free smoke run:
SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
    ./build/gb_sdl tests/diagnostic/background.gb --frames 2
```

`--frames N` is optional and primarily intended for automated checks. It requires
a positive integer and does not change cycles per instruction or PPU timing.
A ROM that never enables its LCD will never reach this limit; automated callers
should provide a timeout. Normal sessions have no instruction/frame limit.

ROM loading happens before SDL initialization. Invalid paths, malformed CLI
arguments and SDL startup failures return failure cleanly. Partial renderer or
texture creation failures follow the same cleanup path as normal exit.

Current results are recorded in [PROGRESS.md](PROGRESS.md). Automated SDL tests
use the dummy video backend; desktop X11/Wayland/GPU presentation still needs
manual inspection on a machine with a real display. Sanitizer runs disable leak
checking because this execution environment cannot run LeakSanitizer under its
tracing setup; AddressSanitizer and UndefinedBehaviorSanitizer remain enabled.

## Optional graphics trace

Build with `-DGB_ENABLE_GRAPHICS_TRACE=ON` and run with `--graphics-trace` to
correlate a selected PPU pixel with its uploaded SDL ARGB value.
`--trace-pixel X,Y` selects another pixel and `--trace-frames N` bounds logging.
This is separate from `--frames N`, which stops the application. See
[GRAPHICS_TRACE.md](GRAPHICS_TRACE.md) for the complete ROM-to-window path.

## Audio playback

`src/frontend/sdl_audio.c` independently owns an SDL queued audio device. The
runner drains core-generated 48 kHz stereo PCM after bounded CPU batches, even
without VBlank or with the LCD off. No SDL callback accesses emulator state;
playback does not tick or pace the core. `--mute` skips the device, and device
initialization failures fall back to silent execution. Shutdown closes audio
before the video frontend releases SDL. See [AUDIO.md](AUDIO.md) for channel
behavior, buffering, tests and remaining approximations.
