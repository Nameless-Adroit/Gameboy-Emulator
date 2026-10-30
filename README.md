# Game Boy CPU learning project

See [PROGRESS.md](PROGRESS.md) for the development log, verification results and
remaining hardware gaps.
See [PPU.md](PPU.md) for PPU functions, bus/CPU communication, timing contracts
and hardware approximations.

This revision implements all **244 ordinary base instructions**, the **CB prefix
and all 256 CB instructions**, and lock-up handling for the 11 illegal base bytes.
It includes a mapped memory bus, timer and basic DMG background/Window/sprite renderer; it is
not yet a complete Game Boy emulator.
See [REVIEW.md](REVIEW.md) for the original-code assessment and
[OPCODE_AUDIT.md](OPCODE_AUDIT.md) for all 512 opcode-table entries.

## SDL window

The optional SDL2 frontend displays completed PPU frames with nearest-neighbor
integer scaling. On Ubuntu:

```sh
sudo apt-get install libsdl2-dev
cmake -S . -B build -DGB_ENABLE_SDL=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/gb_sdl tests/diagnostic/background.gb
```

Close the window or press Escape to quit. See [SDL_FRONTEND.md](SDL_FRONTEND.md)
for setup, presentation/timing separation, comments on each function and tests.
SDL is disabled by default; the headless build below requires no SDL.

## Build and run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/gb tests/gb_opcode_test.gb --trace
```

Or use your existing GCC workflow:

```sh
gcc -std=c99 -Wall -Wextra -Wpedantic cpu.c memory.c emulator.c -o gb
./gb tests/gb_opcode_test.gb --trace
gcc -std=c99 -Wall -Wextra -Wpedantic -I. cpu.c memory.c tests/test_cpu.c -o cpu_tests
./cpu_tests
```

Do rebuild from source. The old tracked `emulator` binary in the original
repository is stale and is deliberately excluded from the delivery archive.

## CPU organization

* Public register, flag, fetch and stack helpers retain their original names.
* `cpu_step()` now returns **T-cycles** and records them in `cycles` and
  `total_cycles`. CPU fetches, reads, writes and internal machine cycles advance
  peripherals through one clock helper. The returned cycles are already clocked;
  callers must not pass them to `memory_tick()` again. Raw `memory_read()` and
  `memory_write()` never advance time. Public CPU fetch/stack helpers are timed.
* `execute()` decodes regular opcode families, then handles exceptional cases.
  Grouping is intentional: e.g. LD B,C and LD D,E share flag/timing behavior.
* `execute_cb()` divides the byte into group, bit/operation and operand fields.
  It covers all 256 combinations, not just a selected subset.
* ALU helpers explain carry/borrow, DAA, signed offsets and preserved flags.
* Interrupt requests live in IF (`FF0F`) and enables in IE (`FFFF`). IME is
  internal CPU state. Requests are serviced at instruction boundaries.
* HALT idles for 4 T-cycles and wakes when an enabled interrupt is pending.
  STOP idles for 0 T-cycles until a new selected joypad line edge or the explicit
  `cpu_wake_stop()` hook. Illegal instructions enter `CPU_LOCKED` until reset.

## Test reproduction

The ordinary C tests have no external dependencies. To reproduce the independent
instruction-vector run (requires Git, Python 3 and a GCC-compatible compiler):

```sh
git clone https://github.com/SingleStepTests/sm83.git ../sm83-tests
python3 tests/run_vectors.py ../sm83-tests/v1
```

Reviewed vectors commit: `f9c30210245dd691661db39f5ace022c465ecc2f`.
The script checks registers, listed final RAM and aggregate timing, not the
individual bus accesses. It deliberately follows upstream guidance to exclude
IME/IE from the vectors; the local C tests check interrupt sequences instead.
STOP/HALT vectors are excluded and replaced by explicit local state tests.

## Manual ROM compatibility

On **2026-10-29**, the user confirmed playing **`Tetris.gb` with sound** in the
emulator, extending the earlier successful launch milestone. This is a
user-reported manual gameplay and audio result; long-session stability and
hardware-accurate audio reproduction have not been established.
The ROM revision and checksum were not recorded, so this result applies to the
user's tested copy. See [PROGRESS.md](PROGRESS.md) for the milestone entry.

## Hardware still needed

The bus maps cartridge ROM, VRAM, WRAM and its echo, OAM, HRAM and I/O.
ROM writes are protected; timer and LCD registers have device-specific handlers.
CPU-facing access enforces LCD VRAM/OAM mode restrictions and HRAM-only access
during active DMG OAM DMA. Raw bus access remains available to devices/tests.
The cartridge module supports ROM-only and standard MBC1 banking up to 2 MiB,
with gated/banked external RAM and battery saves in the SDL runner. The loader
checks exact declared sizes and board limits before replacing a cartridge.
[MBC1.md](MBC1.md) explains banking, `.gb.sav` files and unsupported mapper types.

Timer/DIV is implemented and continues clocking during HALT, including timer
interrupt requests. The PPU implements LCD registers, 456-dot scanlines, 154-line
frames, VBlank and rising-edge STAT interrupts. It shares `memory_tick()` with
the timer; raw bus access remains untimed. LCD reset starts disabled.

The PPU uses fixed timing: mode 3 has a 172-dot duration.
Background scanlines are drawn into the framebuffer at the end of mode 3, using
bus-owned VRAM, 2bpp tiles, both tile maps/addressing modes, BGP and SCX/SCY.
`frame_ready` signals that all visible rows have completed at VBlank. Variable mode 3
timing, LCD startup quirks, the line-153 LY quirk and DMG STAT-write glitch are
not implemented.

Cycle-progressive OAM DMA is implemented. See [DMA.md](DMA.md) for timing and
bus policy. Disconnected DMG serial transfers shift one bit per 512 T-cycles,
receive FF after eight bits, clear SC bit 7 and request the serial interrupt.
External-clock requests wait indefinitely without a peer. Link peers, CGB fast
serial, divider-aligned startup/races, MBC2/MBC3/MBC5 and RTC remain unsupported.
The test-ROM stdout byte sink now emits on completed serial transfers.
The runner stops on STOP/lock-up or the step limit. Run `./build/ppu_tests` for
PPU register, timing, interrupt and clock tests.

The headless runner offers bounded CPU diagnostics independently of the graphics
trace build option:

```sh
./build/gb roms/Mortal_Kombat.gb --steps 10000000 \
  --cpu-trace-start 4000000 --cpu-trace-count 100 --cpu-trace-every 1 \
  > /tmp/mk-output.log 2> /tmp/mk-cpu.log
```

Snapshots go to stderr before each selected step, with PC, SP, the current CPU
bus opcode byte, CPU state, T-cycle count, IME/IE/IF, LY/STAT/LCDC, the selected
4000–7FFF ROM bank, timer/serial registers and a framebuffer hash. `--cpu-trace`
uses start 0, count 256 and stride 1; counts are limited to 4096, followed by
one final snapshot and frame/change totals. Reads add no emulated time. A
HALTed CPU or pending interrupt may not execute the displayed opcode byte.
Frame hashes are diagnostic fingerprints, not proof of pixel accuracy.

On 2026-10-30, Mortal Kombat's MIDWAY splash stall was traced to a missing serial
completion interrupt: its polling loop at 0E24–0E28 waited for an ISR-owned WRAM
flag. With the serial fix the same ROM reaches the title screen. MBC1, CPU,
timer and PPU behavior were not changed for this fix. See [PROGRESS.md](PROGRESS.md)
for the ROM checksum, evidence, test results and remaining uncertainty.

CPU bus accesses consume four T-cycles each; internal cycles occur explicitly
between accesses. Bus reads/writes sample or commit at the end of their machine
cycle. This is not full T-cycle hardware accuracy: interrupt cancellation races,
OAM corruption, detailed STOP behavior (DIV/input quirks,
nonzero second byte), and CGB KEY1 speed switching are not implemented. Reset
uses the existing conventional DMG post-boot CPU registers, not a boot ROM or
complete post-boot peripheral initialization.

## Background diagnostic

[Diagnostic ROM and generation instructions](tests/diagnostic/README.md) provide
shade bars, checkerboard and X tiles. Export the actual CPU-rendered framebuffer:

```sh
./build/bg_diagnostic_tests tests/diagnostic/background.gb build/background.pgm
```

![Background diagnostic preview](tests/diagnostic/background.png)

## Graphics pipeline trace

An optional build-time and runtime trace follows one pixel from CPU ROM reads
and VRAM writes through tile decoding, BGP, framebuffer and SDL presentation.
It defaults to pixel `(20,40)` and bounded output over two rendering passes.
See [GRAPHICS_TRACE.md](GRAPHICS_TRACE.md) for build flags, the flow diagram,
actual diagnostic records and a worked pixel example.

## Window diagnostic

The Window overlays its own tile map at `WX-7, WY` and uses an internal row
counter independent of SCX/SCY. Run a moving background behind a fixed panel:

```sh
cmake --build build
./build/gb_sdl tests/diagnostic/window.gb
```

[WINDOW.md](WINDOW.md) explains placement, counter behavior, tests and the
remaining fixed-timing and hardware-quirk approximations.

## DMG sprites

Mode 2 selects up to ten objects from bus-owned OAM. The compositor supports
8×8/8×16 sprites, transparency, both flips, OBP palettes and DMG priority over
BG/Window pixels. See [SPRITES.md](SPRITES.md) for design, tests and remaining
DMA/fetch-timing approximations.

## Keyboard input

The SDL frontend connects FF00 joypad input to arrow keys (D-pad), Z (A), X (B),
Enter (Start) and Backspace (Select). Escape closes the window. Focus loss clears
held buttons. [JOYPAD.md](JOYPAD.md) documents row selection, interrupts, STOP
wake, tests and input limitations.

## Audio

All four DMG sound channels are connected to the shared hardware clock and SDL2
stereo playback. Audio runs independently of LCD rendering. Use `--mute` for
silent playback; headless builds remain SDL-independent.

```sh
./build/gb_sdl tests/diagnostic/audio.gb
```

The diagnostic repeats left pulse, right pulse, stereo wave and noise stages.
[AUDIO.md](AUDIO.md) documents the separate register/channel/mixer/playback
modules, tests and remaining hardware approximations.
