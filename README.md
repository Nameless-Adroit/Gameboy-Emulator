# Game Boy CPU learning project

See [PROGRESS.md](PROGRESS.md) for the development log, verification results and
remaining hardware gaps.
See [PPU.md](PPU.md) for PPU functions, bus/CPU communication, timing contracts
and hardware approximations.

This revision implements all **244 ordinary base instructions**, the **CB prefix
and all 256 CB instructions**, and lock-up handling for the 11 illegal base bytes.
It includes a mapped memory bus, timer and basic DMG background renderer; it is
not yet a complete Game Boy emulator.
See [REVIEW.md](REVIEW.md) for the original-code assessment and
[OPCODE_AUDIT.md](OPCODE_AUDIT.md) for all 512 opcode-table entries.

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
  STOP idles for 0 T-cycles until `cpu_wake_stop()`; a future input controller
  must call that hook. Illegal instructions enter `CPU_LOCKED` until reset.

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

## Hardware still needed

The bus maps cartridge ROM, VRAM, WRAM and its echo, OAM, HRAM and I/O.
ROM writes are protected; timer and LCD registers have device-specific handlers.
CPU access restrictions during PPU modes and DMA are not yet implemented.
The ROM loader rejects files larger than 32 KiB instead of silently truncating.
A 32 KiB file fitting the loader is not a guarantee of cartridge compatibility.

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

No DMA, full MBC, joypad, APU or accurate serial device is implemented. The runner
prints the common serial-test convention and stops on STOP/lock-up or the step
limit. Run `./build/ppu_tests` for PPU register, timing, interrupt and clock tests.

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
