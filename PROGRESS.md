# Emulator progress

Append a dated entry after each meaningful implementation or verification task.
Record what changed, what was actually tested, results, and remaining limitations.
Keep earlier entries; distinguish previous results from tests rerun today.
Dates use Africa/Dar_es_Salaam time.

## Current status

- CPU bus accesses and internal cycles share one hardware clock; raw memory
  reads/writes never advance time.
- Timer/DIV and basic PPU register/timing/interrupt behavior are connected to it.
- Basic DMG background scanlines render 2bpp tiles from bus-owned VRAM, with
  both maps/addressing modes, BGP and SCX/SCY, into the 160 × 144 framebuffer.
- Cartridge support remains limited to fixed 32 KiB mapping without external RAM.
- The cartridge loader defect has been fixed by rejecting unsupported RAM-bearing
  mapper types. The current Debug and ASan/UBSan CTest suites pass (7/7 each);
  LeakSanitizer is unavailable in this traced execution environment.

## 2026-10-28 — CPU clock refactor (previous completed work)

- Added timed CPU fetch/read/write helpers and explicit internal cycles.
- Removed the runner's duplicate instruction-level hardware tick.
- Added timer-access and stack-cycle regression checks.
- Previously verified: all 11 individual `cpu_instrs` ROMs, `instr_timing`, and
  all three individual `mem_timing` ROMs printed `Passed` (15/15).
- CPU, memory and timer unit tests passed. Cartridge failure remained unchanged.

## 2026-10-28 — PPU foundation (previous completed work)

- Added `src/ppu.c`, header guards/type includes, STAT line tracking and queued
  interrupt requests. Wired LCD registers into the bus and PPU dots into the
  shared hardware clock. Register writes deliver queued IRQs without ticking.
- Implemented fixed mode transitions, LY/LYC coincidence, LCD enable/disable,
  VBlank requests and the combined STAT line's rising-edge behavior.
- Previously verified: PPU/CPU/memory/timer tests passed, including sanitizer
  checks; all 15 CPU/instruction/memory timing ROMs still printed `Passed`.
- This established a timing/register model, not a complete graphics processor.

## 2026-10-28 — PPU timing and interrupt verification

### Changes

- Expanded `tests/test_ppu.c`; production PPU behavior did not change.
- Added an independent frame schedule covering every dot over two full frames,
  all 16 STAT source masks, and LYC values 0, 87, 144 and 153:
  **8,988,736 state/interrupt samples**, including initial states.
- Added LCD disable/re-enable, immediate STAT/LYC write requests, IF clearing,
  mode-3 source suppression and simultaneous interrupt delivery tests.
- Created this progress log and a repository instruction to maintain it.

### Verified behavior within the current model

| Behavior | Verified expectation |
| --- | --- |
| Visible scanlines | Mode 2 at dots 0–79, mode 3 at 80–251, mode 0 at 252–455 |
| Scanline/frame lengths | 456 dots per line, 154 lines, 70,224 dots per frame |
| VBlank | Starts at LY=144 after 65,664 dots; requests IF bit 0 once per frame |
| STAT | Enabled mode/LYC conditions share one line; only rising edges request IF bit 1 |
| STAT blocking | Overlapping sources do not generate duplicate requests |
| Coincidence | LY=LYC updates STAT bit 2 and participates in edge detection |
| Register writes | STAT/LYC writes can request IRQs immediately without elapsed time |
| IF handling | Clearing IF while STAT remains high does not reassert it; other IF bits survive |
| LCD disabled | LY/dot reset to zero, reported mode is 0, PPU stops, STAT line is inactive |
| LCD re-enabled | Current model starts LY=0/dot=0/mode 2; existing IF requests are preserved |
| CPU integration | VBlank/STAT wake HALT; IME controls dispatch; VBlank has priority over STAT |
| Shared clock | Timer, STAT and VBlank requests coexist in one clock batch without losing IF bits |
| Tick batching | One batch and one-dot stepping produce matching state and interrupt flags |

### Validation

- `cmake --build build -j2`: passed.
- Full Debug CTest run: PPU, timer, CPU and memory passed; cartridge test failed
  at its pre-existing `!load_rom(...)` assertion (4/5 suites passed).
- After the final interrupt tests, Debug CTest excluding the known cartridge
  failure: 4/4 passed.
- AddressSanitizer/UndefinedBehaviorSanitizer build and CTest excluding the known
  cartridge failure: 4/4 passed (`ASAN_OPTIONS=detect_leaks=0`; leak checking was
  disabled for this run).
- The 15 ROM results above are from the previous PPU integration run, not rerun
  for this test-only change. No dedicated PPU acceptance ROM suite was run.

Reproduce:

```sh
cmake --build build -j2
ctest --test-dir build --output-on-failure
# Isolate PPU checks:
ctest --test-dir build --output-on-failure -R '^PPU_Tests$'
```

### Remaining hardware gaps

Passing these tests confirms the documented simplified model, not complete
hardware timing accuracy. Mode 3 is fixed at its minimum 172 dots; scrolling,
window and sprite penalties require rendering/fetcher work. LCD startup/first
frame behavior, the line-153 LY early reset and DMG STAT-write glitch are absent.
CPU VRAM/OAM access restrictions, DMA, tile/window/sprite rendering and complete
post-boot initialization are also unfinished. `frame_ready` marks a VBlank
boundary, not a newly rendered image.

### References

- [Pan Docs: rendering and mode durations](https://github.com/gbdev/pandocs/blob/master/src/Rendering.md)
- [Pan Docs: VBlank, STAT and STAT blocking](https://github.com/gbdev/pandocs/blob/master/src/Interrupt_Sources.md)
- [Pan Docs: LCD status registers and STAT-write quirk](https://github.com/gbdev/pandocs/blob/master/src/STAT.md)

## 2026-10-28 — PPU integration review and function documentation

### Changes and review

- Reviewed `ppu.c`/`ppu.h`, memory ownership and address routing, the CPU's timed
  accesses, the runner and CTest registration. Preserved the existing architecture:
  `cpu_tick()` calls `memory_tick()`, which advances timer and PPU together;
  raw bus operations stay untimed. No second runner tick was introduced.
- Replaced PPU timing/interrupt literals with named private constants and made
  `ppu_read()` accept a const state pointer. Clarified state ranges and the IF
  pointer contract in the public header. C99 and the public component boundaries
  remain intact.
- Extended `ppu_tests` with an instruction-driven `LDH` sequence that writes SCX,
  reads LY across a scanline boundary, and writes LYC to request STAT immediately.
  It checks both peripheral and CPU elapsed time to catch duplicate clocking.
- Added [PPU.md](PPU.md), documenting every PPU function, the relevant CPU/bus
  functions, register routing, queued IRQ delivery, the CPU communication path,
  reproduction commands and hardware approximations. Linked it from README.
- No tile renderer, sprites, frontend, DMA or cartridge changes were made.

### Complete regression results (rerun for this review)

| Suite | Debug | Address/undefined-behavior sanitizer build |
| --- | --- | --- |
| PPU_Tests | Passed | Passed |
| Timer_Tests | Passed | Passed |
| CPU_Instruction_Tests | Passed | Passed |
| Memory_Bus_Tests | Passed | Passed |
| Cartridge_Tests | Existing assertion failure at test_cartridge.c:96 | Same existing assertion failure |

Both complete CTest runs finished **4/5 passed**; neither run excluded the
cartridge suite. Sanitizer checks used `ASAN_OPTIONS=detect_leaks=0`; leak
checking was disabled. No sanitizer diagnostics were emitted by the passing
suites. The sanitizer build still reports existing CPU integer-conversion
warnings; the PPU changes produced no compiler warnings.

Reran all 15 supported ROM regressions using the rebuilt `build/gb`:

- `cpu_instrs/individual`: **11/11 passed**.
- `instr_timing/instr_timing.gb`: **passed**.
- `mem_timing/individual`: **3/3 passed**.

ROM results require serial `Passed`, no serial `Failed`, and process exit 0;
exit status alone is insufficient. Current-run logs and the JSON summary are
under `/tmp/gb-ppu-review-results/` (temporary artifacts).
`git diff --check` also passed.

### Findings and limits

No new functional regression was found within the documented fixed-mode model.
The cartridge failure remains the loader/test policy mismatch described above,
not a PPU failure. Fixed mode-3 duration, simplified LCD startup, line-153 LY
behavior, missing STAT-write glitch and missing VRAM/OAM restrictions prevent a
claim of hardware-accurate PPU behavior. Dedicated hardware PPU acceptance ROMs
were not run. The exhaustive unit checks confirm the baseline schedule rather
than compensating for those missing hardware behaviors.

## 2026-10-28 — Cartridge fix, 2bpp decoding and background rendering

### Changes

- Fixed the cartridge loader defect: it incorrectly accepted RAM-bearing MBC1
  types 02/03 despite implementing no cartridge RAM. Restored its documented
  supported types 00/01. Header decoding remains independent of mapper support.
  Rejected loads preserve the previous cartridge. Kept the original failing
  assertion and added explicit 02/03 rejection/preservation cases.
- Added pure row and full-tile DMG 2bpp decoders. Bit 7 is the left pixel;
  the first row byte is the low bitplane and the second is the high bitplane.
- Bound the PPU to a const view of bus-owned VRAM after initialization. No
  allocation, CPU pointer, circular bus dependency or extra hardware ticks.
- Added background scanline rendering at the existing dot-252 boundary. Supports
  LCDC background enable, both 32×32 maps, unsigned 8000 and signed 9000 tile
  addressing, BGP shade mapping and SCX/SCY scrolling with 256-pixel wraparound.
- Framebuffer entries now hold DMG shades 0–3 (white through black). LCD disable
  clears it; VBlank/frame_ready occurs after all visible rows have rendered.
- Added `PPU_Render_Tests` and `BG_Diagnostic_ROM` CTest suites. Kept all prior
  PPU timing/STAT/HALT/priority tests without changing their expectations.
- Added a reproducible 32 KiB ROM-only diagnostic, its standard-library Python
  generator and a headless CPU-execution/image-export harness. The diagnostic
  uses five tiles to draw a border, grayscale bars, checkerboard and X pattern.
- Updated [PPU.md](PPU.md) with function responsibilities, VRAM lifetime rules,
  tile/address/palette data paths and design decisions. Updated README and
  [diagnostic instructions](tests/diagnostic/README.md). No SDL, sprites, window
  layer, DMA or cycle-accurate pixel FIFO was added.

### Verification

- Complete Debug CTest: **7/7 passed**, including the formerly failing cartridge
  test, all existing CPU/timer/memory/PPU tests and the two new suites.
- Complete ASan/UBSan CTest: **7/7 passed** with `ASAN_OPTIONS=detect_leaks=0`.
  No address/undefined-behavior sanitizer diagnostics were reported.
- The first sanitizer run with default leak checking failed **7/7** at process
  exit because LeakSanitizer cannot run under this environment's tracing/ptrace
  setup. Those infrastructure failures are not hidden: the passing rerun disables
  leak checking only, retaining address and undefined-behavior instrumentation.
  Leak checking remains unverified. Existing CPU integer-conversion compiler
  warnings remain; no new PPU compiler warnings were introduced.
- All 11 individual `cpu_instrs` ROMs, `instr_timing`, and all three individual
  `mem_timing` ROMs were rerun in **both Debug and sanitizer builds: 15/15 passed
  in each build**. Serial output and process exit status were checked. Temporary
  detailed logs: `/tmp/gb-background-regressions/{debug,sanitizer}/`.
- Decoder coverage includes all 65,536 bitplane pairs, a published sample,
  asymmetric plane/pixel ordering and full-tile row ordering.
- Renderer coverage compares every framebuffer pixel for 120 combinations of
  maps, addressing modes, fine/coarse/wrapping scroll offsets and palettes.
  Distinct fixtures distinguish signed/unsigned tile blocks, including IDs
  00/7F/80/FF. Tests also verify row commit timing, between-row VRAM/palette
  changes, disabled BG white, LCD reset, detached state and batch equivalence.
- Diagnostic test executes the generated ROM through CPU/bus/PPU to first
  VBlank and checks **all 23,040 pixels**. Exported and visually inspected the
  actual framebuffer as [a 4× PNG preview](tests/diagnostic/background.png).
  The ROM regenerates byte-for-byte, and both header/global checksums validate.
- `git diff --check`: passed.

### Remaining approximations

The DMG tile encoding, map selection, signed/unsigned tile addresses, palette
mapping and scrolling wrap rules are implemented directly. Raster timing remains
simplified: a whole scanline samples VRAM/registers at dot 252, mode 3 is fixed at
172 dots, and mid-scanline changes are not pixel-accurate. LCD startup/first-frame
blanking, line-153 LY details, STAT-write glitches and CPU VRAM/OAM restrictions
are still missing. Window/sprite/FIFO/frontend work remains out of scope. No
physical DMG hardware or dedicated hardware PPU acceptance ROM suite was used.

Reproduce:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j2
ctest --test-dir build --output-on-failure
cmake -S . -B /tmp/gb-ppu-sanitized -DGB_ENABLE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/gb-ppu-sanitized -j2
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir /tmp/gb-ppu-sanitized --output-on-failure
./build/bg_diagnostic_tests tests/diagnostic/background.gb build/background.pgm
```
