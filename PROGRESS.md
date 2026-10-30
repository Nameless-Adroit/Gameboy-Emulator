# Emulator progress

Append a dated entry after each meaningful implementation or verification task.
Record what changed, what was actually tested, results, and remaining limitations.
Keep earlier entries; distinguish previous results from tests rerun today.
Dates use Africa/Dar_es_Salaam time.

## Current status

- Four-channel DMG audio uses the shared divider/clock, bounded stereo PCM and
  separate SDL playback. See [AUDIO.md](AUDIO.md) for hardware approximations.

- User-reported manual compatibility milestone: `Tetris.gb` played with sound
  on 2026-10-29. ROM revision, exact audio fidelity and long-session stability
  remain unverified.

- FF00 joypad matrix/interrupts are connected to SDL keyboard events, with
  focus-safe releases, HALT handling and basic selected-edge STOP wake.

- Mode 2 selects up to ten sprites from real OAM; the shared compositor supports
  DMG sprite sizes, flips, transparency, OBP palettes and BG/Window priority.

- OAM DMA copies progressively through the shared clock; CPU grants enforce
  HRAM-only active DMA access and LCD-mode VRAM/OAM restrictions. Raw bus
  access remains untimed and available to devices/tests.

- Optional bounded graphics tracing follows one pixel from bus accesses to PPU
  output and SDL presentation; compile-time and runtime defaults are off.

- Optional SDL2 `gb_sdl` frontend presents completed frames with integer nearest-
  neighbor scaling; the core and headless build remain SDL-independent.

- CPU bus accesses and internal cycles share one hardware clock; raw memory
  reads/writes never advance time.
- Timer/DIV and basic PPU register/timing/interrupt behavior are connected to it.
- DMG background and Window scanlines share 2bpp decoding, maps/addressing modes,
  BGP and the 160 × 144 framebuffer. Window positioning/counters are independent
  of BG scrolling; fixed timing and documented hardware quirks remain simplified.
- Standard MBC1 supports ROM banking up to 2 MiB, both modes, gated/banked RAM,
  and SDL battery saves. MBC2/MBC3/MBC5/RTC and MBC1M remain unsupported.
- Fifteen headless suites pass; SDL + trace suites pass (22/22 in Debug and
  ASan/UBSan). Newly unblocked sound singles pass 9/12; wave tests 09/10/12 fail;
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

## 2026-10-28 — Optional minimal SDL2 frontend

### Changes

- Added an opt-in `GB_ENABLE_SDL` CMake option, `gb_sdl_frontend` presentation
  library and separate `gb_sdl` executable. Default headless builds need no SDL.
- Kept CPU, PPU, memory, timer and the original headless runner unchanged.
  `gbcore` has no SDL dependency; SDL headers/types stay in `src/frontend/`.
- Added a resizable 3× window, explicit nearest-neighbor texture filtering,
  integer logical scaling and letterboxing. The frontend converts existing
  shade IDs 0–3 to opaque ARGB8888 host pixels without modifying PPU pixels.
- Present completed frames only when the runner observes `frame_ready`, then
  acknowledge the latch after a successful upload/draw. Expose/resize events
  redraw the cached uploaded texture instead of sampling a partial next frame.
- Added Close/Escape handling, bounded event polling independent of VBlank,
  partial-startup cleanup and reverse-order shutdown. STOP keeps the display
  responsive; locked CPUs report failure. No joypad mapping was added.
- Host-only pacing compares elapsed performance-counter time with existing CPU
  T-cycle totals. It sleeps without ticking devices, skipping CPU instructions,
  changing PPU mode lengths or tying hardware clocks to monitor refresh.
- Added detailed inline comments and [SDL_FRONTEND.md](SDL_FRONTEND.md) covering
  Ubuntu setup, each function, lifecycle, rendering format and timing separation.
  Linked build/run instructions from README.

### Verification

- Default SDL-disabled Debug build: **7/7 existing suites passed**. Verified
  `build/gb` links no SDL library.
- SDL-enabled Debug build: **9/9 CTest suites passed**, including all seven
  existing suites, `SDL_Frontend_Tests` and `SDL_Diagnostic_Smoke`.
- SDL-enabled ASan/UBSan build: **9/9 passed**, no sanitizer diagnostics.
  Used `ASAN_OPTIONS=detect_leaks=0` because LeakSanitizer cannot operate in this
  traced environment. Existing CPU integer-conversion warnings remain; no new
  frontend compiler warnings were emitted.
- Frontend tests exercise actual SDL objects with dummy video/software rendering:
  four shade conversions, exact 3× output pixel replication, nearest texture
  mode, integer logical scaling, read-only PPU access, cached redraws, Escape,
  SDL_QUIT, window-close events and repeat-safe cleanup.
- End-to-end smoke test runs the existing diagnostic ROM and successfully
  presents two frame-ready images before clean exit.
- Manual subprocess checks: nonexistent ROM, invalid negative frame limit and
  deliberately unavailable SDL video driver each exit with status 1 and an
  informative error, without hanging.
- `git diff --check`: passed. The separate 15-ROM batch was not rerun for this
  frontend-only change; previous results remain in earlier entries.

### Environment and limitations

SDL2 development files were absent. The first dependency download was blocked
by sandbox networking; after approval, downloaded Ubuntu SDL2 2.32.10 packages
and the missing libXss dependency into `/tmp/gb-sdl-deps/`, without installing
system packages. Local extracted-package builds used their CMake prefix,
architecture-header include path and runtime library path. Initial dependency
build failures were resolved by providing those relocated headers/runtime files.
Actual Ubuntu users can install `libsdl2-dev` normally as documented.

All SDL tests used the dummy video backend; desktop/GPU window presentation has
not been visually tested. No SDL frontend changes were made to the existing
hardware approximations. The frontend retains the last image while LCD is off
or CPU is stopped, until a new completed frame or host close event. Host pacing
is basic, with no audio synchronization. SDL 2.0.12 or newer is required.

For a normal Ubuntu installation:

```sh
sudo apt-get install libsdl2-dev
cmake -S . -B build -DGB_ENABLE_SDL=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j2
./build/gb_sdl tests/diagnostic/background.gb
ctest --test-dir build --output-on-failure
```

## 2026-10-28 — Optional Graphics Pipeline Trace Mode

### Changes

- Added `GB_ENABLE_GRAPHICS_TRACE` (default OFF) and runtime `--graphics-trace`,
  `--trace-pixel X,Y` and `--trace-frames N` options to headless and SDL runners.
  Defaults are pixel (20,40), two rendering passes. Instrumented builds also
  remain silent unless runtime tracing is requested.
- Added caller-owned trace state with compile-guarded observer pointers in
  Memory/PPU. The default build has no trace hooks/counters in hot paths;
  `cpu_fetch` aliases the existing timed read when trace support is compiled out.
- Trace records cover bounded ROM data reads, CPU VRAM/register writes with
  instruction-start PC, actual selected map/tile/row bytes and decode result,
  BGP-mapped framebuffer shade, VBlank and successfully presented SDL ARGB.
  Opcode/immediate fetches are excluded from ROM data records.
- Observer timestamps accumulate only existing `memory_tick()` durations. PPU
  records include the exact intra-batch dot offset, preserving correlation with
  CPU writes and host presentation without adding any simulated time.
- Added one-based rendering-pass IDs across frame wraps and LCD restarts;
  SDL uses the saved completed-pass ID. Blank sources and raw bus access are
  explicitly labeled rather than given invented tile or CPU provenance.
- Enforced category quotas and a 256-record global event cap plus START header.
  Coordinate/frame parsing rejects invalid values. Detaching resets observer
  references safely. SDL/core ownership and headless behavior remain intact.
- Added [GRAPHICS_TRACE.md](GRAPHICS_TRACE.md): actual function map, data-flow
  diagram, options, bounds, timing semantics and actual diagnostic pixel example.
  It distinguishes Game Boy CPU copies from direct C loader/PPU/SDL work and
  explains the lack of register taint tracking and pixel-FIFO accuracy.

### Results

- Trace compiled out, existing SDL-enabled Debug build: **9/9 passed**.
- Trace + SDL enabled, Debug: **11/11 passed**, including new
  `Graphics_Trace_Tests` and `SDL_Graphics_Trace` suites.
- Trace + SDL enabled, ASan/UBSan: **11/11 passed**, no sanitizer diagnostics.
  `ASAN_OPTIONS=detect_leaks=0` retains the previously documented environment
  limitation: LeakSanitizer cannot run under this session's tracing setup.
  Existing CPU integer-conversion compiler warnings remain unchanged.
- Separate trace-enabled SDL-disabled Debug configuration: **8/8 passed**.
- Trace tests verify signed tile addressing, scrolling and palette provenance;
  exact pixel timestamps inside large clock batches; PC/raw-source labels;
  LCD restart/blank source records; frame/category/global bounds; parser
  validation and observer detachment. A two-frame diagnostic run compares
  traced/untraced CPU, bus, timer, PPU and framebuffer state/cycle results.
- End-to-end SDL trace confirms framebuffer shade 0 -> ARGB FFFFFFFF at (20,40).
  A separate CLI check at (60,40) confirms nonwhite ARGB FFAAAAAA.
- Headless CLI trace completes successfully with bounded records and no SDL
  presentation records. A compiled-out CLI request returns a clear build-option
  error. `nm` inspection confirms the default PPU object has no trace symbols.
- Actual diagnostic trace: LCD enable at cycle 57652, selected pixel committed at
  76144, VBlank and SDL presentation at 123316. Captured records are in the
  worked example in GRAPHICS_TRACE.md.
- `git diff --check`: passed. The 15-ROM batch was not rerun for this observer
  feature; all registered regression suites above were rerun.

### Limits

The existing fixed scanline model remains unchanged; no pixel FIFO, rendering
feature, input, or new hardware timing was added. Category caps intentionally
omit most bus traffic and do not constitute a full instruction/access trace.
Direct array modifications bypass bus records. SDL checks used dummy/software
presentation, not a physical monitor. Library attachment is intended immediately
following memory initialization, before execution; its cycle epoch begins there.

## 2026-10-28 — Graphics log startup diagnosis

- Inspected `build/trace/graphics.log`: it contains the compiled-out tracing
  error, rather than pipeline records. The ordinary `build` cache has graphics
  tracing OFF; the separate `build/trace` cache has it ON.
- Reproduced both paths using SDL dummy/software with the diagnostic ROM:
  `build/gb_sdl --graphics-trace` exits 1 with that exact message;
  `build/trace/gb_sdl --graphics-trace` exits 0 and emits pixel/SDL records.
- The evidence indicates the ordinary executable was run with output redirected
  into the trace directory. Redirecting stderr changes the log destination, not
  the executable or build configuration. No code change or rebuild was needed.
- Preserved the user's existing log. Use `./build/trace/gb_sdl` to generate the
  replacement log; add `--frames 2` for a finite verification session.

## 2026-10-28 — DMG Window layer and scrolling diagnostic

### Implementation

- Added Window composition to the existing scanline renderer, reusing tile-row
  decoding, signed/unsigned tile addressing, BGP mapping and framebuffer storage.
  The Window uses LCDC bit 6 for its own map and bit 5 for enable; DMG bit 0 gates
  both background and Window. Color zero remains opaque.
- Implemented signed `WX-7` positioning, left clipping with retained local tile
  coordinates, right-edge clipping and independent Window X/Y map coordinates.
  SCX/SCY only affect the background.
- Added `window_y_triggered`, latched when WY matches LY at visible line start,
  cleared at VBlank; DMG Window enable toggles and later WY writes do not undo
  the match. LCD transitions reset Window state.
- Added `window_line`, incremented once only after a scanline contains actual
  Window pixels. Offscreen/disabled/master-disabled/detached rows pause it;
  frame wrap and LCD reset restart it. White Window rows still count.
- Preserved fixed mode boundaries and existing CPU, bus, timer, interrupt and
  SDL behavior. Window rendering does not call a clock function.
- Graphics trace now reports the final composed pixel's layer and source once,
  including Window map/tile/row metadata instead of obscured BG provenance.
- Added `PPU_Window_Tests`, `Window_Diagnostic_ROM` and optional SDL Window smoke
  coverage through the existing CMake/CTest structure. Kept C99 compatibility.

### Diagnostic and documentation

- Added `tests/diagnostic/window.gb` and its standard-library-only generator.
  Actual CPU instructions load seven tiles/two maps, set WX=31/WY=80 and enable
  the Window. Its VBlank ISR increments SCX once per frame while the bordered
  W-pattern overlay remains fixed. The original diagnostic remains unchanged.
- Added an end-to-end test checking every pixel across three frames (SCX=0,1,2),
  64 Window rows per frame and correct frame timing. Optional PGM export uses
  the actual CPU-produced framebuffer.
- Exported and visually inspected [window.png](tests/diagnostic/window.png), a
  nearest-neighbor 4× preview of the third test frame. Verified ROM generation
  is byte-for-byte reproducible and header/global checksums are valid.
- Added [WINDOW.md](WINDOW.md) with coordinate/counter rules, architecture,
  examples, references and explicit timing gaps. Updated PPU, trace, README
  and diagnostic documentation.

### Regression results

- SDL-enabled Debug without trace: **12/12 passed**.
- SDL + trace-enabled Debug: **14/14 passed**.
- SDL + trace-enabled ASan/UBSan: **14/14 passed**, no sanitizer diagnostics.
  Used `ASAN_OPTIONS=detect_leaks=0` for the previously documented ptrace/LSan
  environment limitation. Existing CPU integer-conversion compiler warnings
  remain; no new Window compiler warnings were emitted.
- Separate SDL-disabled, trace-disabled headless Debug build: **9/9 passed**.
- Reran all 11 individual CPU instruction ROMs, instruction timing ROM and all
  three individual memory timing ROMs: **15/15 passed**. Logs are temporarily
  under `/tmp/gb-background-regressions/window/`.
- Window tests compare 800 full-frame combinations, covering both maps, both
  addressing modes, ten WX positions (0/1/6/7/8/23/165/166/167/255), five WY
  values (0/5/143/144/255), fine/wrapping BG scroll and two palettes. Added
  counter pause/resume, enable gating, early/late WY matching, opacity, detached
  VRAM, reset/frame-wrap, batch equivalence and unchanged mode-boundary checks.
- Actual traced diagnostic pixel (40,88): Window map 9C22, tile 06, row 8060,
  bytes 81/81, color/shade 3, SDL ARGB FF000000 across both sampled frames.
  Trace unit coverage verifies one Window pixel record with correct provenance.
- `git diff --check`: passed. No current test failures.

### Remaining hardware inaccuracies

Mode 3 is still a fixed 172 dots: Window activation/fetcher penalties are not
modeled. Each composed row samples state at dot 252; repeated starts and
pixel-level WX/WY/LCDC changes are not modeled. WX=0 uses geometric clipping
instead of SCX-dependent hardware behavior, and WX=166 uses one-pixel clipping
instead of the DMG full-width/vertical-offset quirk. Disabled-Window glitch
pixels, FIFO effects and existing LCD startup/LY/STAT/VRAM-access gaps remain.
No sprites, APU or cycle-accurate pixel FIFO was added. SDL tests used the
headless dummy/software backend; no physical DMG hardware was tested.

Run the new diagnostic:

```sh
cmake --build build -j2
./build/gb_sdl tests/diagnostic/window.gb
./build/window_diagnostic_tests tests/diagnostic/window.gb build/window.pgm
```

## 2026-10-29 — Progressive OAM DMA and CPU bus restrictions

### Implementation

- Added `include/dma.h` / `src/dma.c` and embedded DMA state in Memory. FF46
  schedules a transfer/restart instead of copying OAM immediately. Current model:
  eight startup T-cycles, then 160 bytes at four T-cycles per byte (640 active
  transfer cycles, 648 total sampled-write-to-completion cycles).
- Restart preserves existing OAM and lets the previous transfer advance during
  the new startup delay; the latest pending FF46 request wins. Each byte reads
  its source live at transfer time. No new allocation or independent clock.
- Added untimed `memory_cpu_read/write()` policy wrappers and routed timed CPU
  data, fetch and stack helpers through them. Active DMG DMA allows HRAM only,
  with an FF46 write exception for restart. Blocked reads return FF; writes are
  ignored. Internal IF/IE interrupt-controller operations remain raw.
- Added CPU PPU-mode restrictions: VRAM blocked in mode 3, OAM blocked in modes
  2/3, both accessible with LCD disabled or during HBlank/VBlank. Raw bus accesses
  remain untimed and available to test fixtures, debuggers and devices.
- Added `memory_dma_read()` to use the raw mapped source without falsely logging
  autonomous DMA reads as CPU ROM graphics reads.
- Integrated DMA into `memory_tick()`: pending/active transfers interleave timer,
  PPU and DMA per T-cycle, then return to batched ticking immediately when DMA
  ends. All share the same elapsed time; trace observer time increments once.
- Added [DMA.md](DMA.md) documenting timing, APIs, CPU ownership, source pages,
  startup/restart assumptions and remaining inaccuracies. Updated related
  architecture and README documentation.

### Transfer tests and regressions

- `DMA_Tests` verifies startup/byte/completion boundaries, live reads, restart,
  source mapping, absent/unsupported sources, no overrun and batching equivalence.
- CPU tests execute a real HRAM DMA/wait routine, verify blocked data/stack
  writes, blocked ROM fetch FF/RST 38, actual CPU FF46 restart, HRAM permissions,
  CPU OAM reads at adjacent completion boundaries and VRAM mode-boundary reads.
- HALT integration checks DMA completion with simultaneous timer/VBlank
  requests, unchanged CPU cycle totals and correctly advancing PPU time.
- Trace-enabled test verifies DMA ROM reads are not mislabeled as CPU reads.
- SDL-enabled Debug, trace off: **13/13 CTest suites passed**.
- SDL + trace-enabled Debug: **15/15 passed**.
- SDL + trace-enabled ASan/UBSan: **15/15 passed**, no sanitizer diagnostics.
  Leak checking remains disabled (`ASAN_OPTIONS=detect_leaks=0`) because of the
  existing tracing/ptrace environment limitation. Existing CPU integer-conversion
  compiler warnings remain; no new DMA warnings were emitted.
- Headless, trace-disabled Debug: **10/10 passed**.
- After adding the final CPU completion-boundary assertions, reran DMA_Tests in
  Debug and ASan/UBSan: passed in both.
- Reran all 11 individual CPU instruction ROMs, `instr_timing` and all three
  individual `mem_timing` ROMs: **15/15 passed**. Temporary detailed logs are in
  `/tmp/gb-background-regressions/dma/`.
- `git diff --check`: passed. Existing PPU/Window/renderer, SDL diagnostics,
  timer, memory and cartridge suites remain green.

### Limits

Startup/restart and CPU sampling use the existing machine-cycle access model,
not every hardware pin phase. Source pages E0..FF deterministically supply FF;
undocumented mirroring/open-bus behavior is not modeled. DMA source VRAM reads
currently bypass PPU arbitration. Sprite-fetch/OAM corruption, CGB split buses,
double speed and HDMA remain unimplemented. PPU access limits follow existing
fixed mode lengths. No external DMA hardware-acceptance ROM suite was run; local
schedule tests were informed by Pan Docs and hardware-tested Mooneye sources
linked in DMA.md. No sprite rendering or other unrelated system was added.

## 2026-10-29 — Mode-2 OAM selection and DMG sprite pixels

### Implementation

- Added a borrowed PPU OAM view and DMA-ownership signal, bound in memory_init()
  to the actual bus-owned array/state. CPU writes, completed DMA and PPU selection
  now use the same OAM data, with no extra clock or SDL dependency.
- Mode 2 visits an entry every two dots, keeps the first ten Y-overlapping
  objects in OAM order and snapshots Y/X/tile/flags/height/index. X-hidden entries
  still count. Line/LCD transitions reset selection; detached OAM is safe.
- Added 8×8 and 8×16 object tile fetching using the existing 2bpp row decoder,
  unsigned 8000 addressing, paired even/odd tiles, full-object Y flip and X flip.
- Added per-pixel DMG priority: first nontransparent object by smaller X and
  earlier OAM tie, then apply the winner's behind-BG flag to the original
  BG/Window color ID. Hidden winners still mask lesser objects. Color zero is
  transparent before OBP mapping; palette-mapped white remains opaque.
- Implemented OBP0/OBP1 selection, clipping, independent LCDC OBJ enable and
  BG-disable behavior that leaves objects enabled. CGB-only flags are ignored.
- Renamed the private combined renderer to render_scanline(). It commits one
  final pixel to the existing framebuffer at fixed dot 252; SDL is unchanged.
- Graphics trace now emits sprite-visible pixels with OAM entry/index, flags,
  original OAM tile, effective row address/bytes, color, OBP and final shade,
  replacing the obscured lower-layer record instead of logging both.
- Added [SPRITES.md](SPRITES.md) with function/state responsibilities, bus binding,
  priority rules and explicit remaining hardware approximations. Updated related
  PPU, Window, DMA, trace and README documentation.

### Tests and results

- Added PPU_Sprite_Tests for two-dot selection progress, dot-80 final entry,
  snapshots, CPU OAM-write rejection, first-ten limit, X/Y-hidden objects,
  LCD resets, detached state and Memory-copy pointer rebinding.
- Compared every pixel across 224 full-frame geometry fixtures spanning sizes,
  both flips, both palettes, clipping, highest/odd tile IDs and BG addressing
  modes. Explicit cases cover transparent zero versus opaque white, ignored
  CGB flag bits, X/OAM ties and raw-color BG/Window priority including masking
  lower sprites behind the background.
- DMA integration tests confirm transferred OAM becomes visible, active DMA
  suppresses affected object rows, and later Mode-2 scans see completed data.
  Batch/single-dot framebuffer, timing and IF behavior agree.
- Trace-enabled test confirms one final sprite record at (20,40) with correct
  OAM/row/color/palette provenance while BG is disabled.
- SDL-enabled trace-disabled Debug: **14/14 CTest suites passed**.
- SDL + trace-enabled Debug: **16/16 passed**.
- SDL + trace-enabled ASan/UBSan: **16/16 passed**, no sanitizer diagnostics.
  Leak detection remains disabled (ASAN_OPTIONS=detect_leaks=0) because of the
  existing ptrace environment limitation. Existing CPU conversion warnings
  remain; no new sprite compiler warnings were emitted.
- Separate headless, trace-disabled Debug build: **11/11 passed**.
- Reran all 11 individual cpu_instrs ROMs, instr_timing and the three individual
  mem_timing ROMs: **15/15 passed**. Temporary logs:
  `/tmp/gb-background-regressions/sprites/`.
- Existing CPU, timer, memory, cartridge, PPU, Window, renderer, DMA, graphics
  trace and SDL diagnostic suites remain green. git diff --check passed.

### Remaining approximations

The fixed 80-dot Mode 2 / 172-dot Mode 3 schedule is retained. Sprite fetch stalls
and FIFO phases are absent; all four OAM attributes/height are captured together
per entry and tile bytes/palettes are sampled at row commit. Mid-scanline OAM,
LCDC-size and palette effects are therefore simplified. Active DMA skips OAM
selection and suppresses object drawing at row commit rather than reproducing
mixed/corrupted object words. CGB sprite priority/banks/palettes are not modeled.
No external sprite hardware acceptance suite or physical DMG was tested. APU and
other unrelated systems were not added.

## 2026-10-29 — FF00 joypad controller and SDL keyboard input

### Implementation

- Added SDL-independent Joypad state/module with eight physical button bits,
  active-low selected row reads, read-only input lines and writable FF00 select
  bits 4/5. Bits 6/7 read high; reset releases all buttons and selects neither row.
- Routed FF00 through the mapped bus rather than the generic I/O byte array.
  CPU accesses retain their timing and DMA grants; host physical input updates
  bypass CPU grants and never advance emulated time.
- Selected high-to-low input transitions OR IF bit 4, preserving other bits.
  Selection changes can expose a held button and request an edge. Both selected
  rows share lines; repeated/held levels and releases do not generate false IRQs.
- Added basic selected-edge STOP wake consumption at CPU instruction boundaries.
  Waking is independent of IE/IME; normal interrupt dispatch still uses them.
  Edges consumed while running do not remain to wake a later STOP. Existing
  explicit cpu_wake_stop() remains supported and instruction cycle counts remain
  unchanged. SDL runner permits wake steps and resets host pacing after STOP.
- Bound SDL input to borrowed Joypad/IF state. Mapping: arrows=D-pad, Z=A, X=B,
  Enter=Start, Backspace=Select; Escape remains host quit. Uses physical scancodes,
  ignores repeats and unfocused presses, and processes taps in event order.
- Focus loss, minimizing, unbinding, quit and destruction release held buttons.
  Core code contains no SDL headers or CPU/frontend callbacks.
- Added [JOYPAD.md](JOYPAD.md) covering controls, register/IRQ rules, diagram,
  APIs, lifetime, STOP and timing limits. Updated README, SDL and CPU docs.

### Verification

- New Joypad_Tests covers all button masks and 262,144 pairwise physical-level
  transitions across all row selections, FF00 masks, selection-generated edges,
  shared lines, IF preservation, CPU LDH access timing, DMA restrictions,
  HALT behavior, vector 0060 and STOP wake/no-stale-wake behavior.
- New SDL_Joypad_Tests injects all eight key mappings, repeat and duplicate
  presses, key release, quick taps, simultaneous shared-line inputs, focus loss,
  minimize/refocus, detach/quit cleanup and an SDL-event-to-CPU STOP wake sequence.
  Input polling itself leaves CPU/timer/PPU cycle counts unchanged.
- Initial Joypad_Tests fixture incorrectly expected an interrupt when switching
  from held Right to held A on their already-low shared line. Corrected the
  fixture to release Right before testing a genuine selection-generated edge;
  the controller correctly produced no edge for the original shared-low case.
- SDL-enabled trace-disabled Debug: **16/16 passed**.
- SDL + trace-enabled Debug: **18/18 passed**.
- SDL + trace-enabled ASan/UBSan: **18/18 passed**, no sanitizer diagnostics.
  Leak checking remains disabled (ASAN_OPTIONS=detect_leaks=0) due to the existing
  ptrace environment limitation. Existing CPU conversion compiler warnings
  remain; no new joypad compiler warnings were emitted.
- Separate headless, trace-disabled Debug configuration: **12/12 passed**.
- Reran all 11 individual cpu_instrs ROMs, instr_timing and three individual
  mem_timing ROMs: **15/15 passed**. Temporary logs:
  `/tmp/gb-background-regressions/joypad/`.
- git diff --check passed. No current test failures.

### Limits

Rows/input levels update immediately; mechanical bounce and electrical settling
are not modeled. STOP wake is the existing simplified CPU model plus selected
input edges, not complete hardware STOP/DIV/startup behavior. SGB protocols,
multiplayer, joysticks and configurable mappings are absent. Opposing directions
can be held together as physical matrix bits. SDL event tests used dummy/software
video, not a physical desktop keyboard session. Headless execution defaults to
released buttons unless its embedding uses the new input API.

## 2026-10-29 — Tetris ROM opens successfully

- The user reported successfully opening `Tetris.gb` in the emulator. This is
  a manual ROM launch milestone following the joypad/SDL input integration.
- Added the result to README's manual ROM compatibility section and the current
  status above. No emulator code or cartridge support was changed.
- Evidence: user observation; this documentation task did not rerun the ROM or
  regression suite. Earlier automated test results remain recorded in their
  original entries.
- Scope: gameplay, keyboard response, audio and long-session stability were not
  reported as tested. The tested ROM's revision, size and checksum are unknown;
  successful opening does not establish compatibility with all Tetris releases.

## 2026-10-29 — Modular DMG audio and SDL playback

### Implementation

- Added SDL-independent APU state and modules for register/power handling,
  channel sequencing, and PCM mixing. Supports two pulse channels (CH1 sweep),
  wave RAM/channel and noise LFSR; DAC enable, length, envelopes, NR50 volume,
  NR51 panning and NR52 activity/power are mapped at FF10–FF3F.
- Routed audio through the existing memory bus and CPU DMA grants. Raw accesses
  remain untimed. The shared memory_tick observes the existing timer divider;
  internal bit 12 falling edges clock length/sweep/envelope. FF04 reset observes
  the same edge without adding cycles. CPU/PPU timing code was not refactored.
- Generates deterministic 48 kHz signed-16 stereo PCM with rational sample
  cadence, box averaging and approximate high-pass filtering. Bounded core
  buffering drops oldest frames with a counter instead of blocking headless use.
- Added separate SDL queued playback module. Main-thread copies keep SDL's audio
  thread away from core state. Playback runs independently of LCD/VBlank, uses
  existing CPU-cycle pacing, clears excessive latency/STOP queues, and cleans up
  before SDL video shutdown. --mute and device-failure silent fallback drain PCM
  without disabling the emulated APU.
- Added dependency-free audio.gb generator: shade-bar display plus repeating
  left pulse, right pulse, stereo triangle wave and stereo noise stages.
- Added AUDIO.md with API/function responsibilities, architecture diagram,
  controls/run instructions, verification and remaining approximations. Updated
  README, PPU/SDL docs and diagnostic instructions.

### Verification performed in this task

- Added APU_Tests: register masks/power, all channel DAC/length behavior,
  pulse/wave/noise timing, retrigger phase/sample behavior, LFSR widths/high
  shifts, length extra clocks, up/down envelopes, sweep overflow/negate,
  DIV resets, stereo routing/volume, batching equivalence, exact sample cadence,
  overflow bounds, CPU LDH cycles, HALT/STOP and DMA grants.
- The first length test exposed an incorrect internal divider-bit selection in
  the new implementation. Corrected bit 13 to bit 12 (DIV register bit 4),
  restoring 512 Hz sequencing. A DMA test fixture initially checked restrictions
  before the existing eight-cycle startup delay elapsed; corrected the fixture
  to wait until DMA actually becomes active, preserving the DMA implementation.
- Added Audio_Diagnostic_ROM: real SM83 instructions drive each channel through
  the bus and produce nonzero PCM; the diagnostic framebuffer is also checked.
- Added SDL_Audio_Tests using the dummy audio driver: copied queue data/draining,
  queue bounds, clearing, repeated shutdown, muted drain and deliberate device
  initialization failure with silent fallback. Physical speaker playback was
  not tested in this environment.
- Complete SDL + graphics-trace Debug CTest: **21/21 passed**.
- Complete SDL + graphics-trace ASan/UBSan CTest: **21/21 passed**; no sanitizer
  diagnostics. Used ASAN_OPTIONS=detect_leaks=0 for the existing ptrace /
  LeakSanitizer environment limitation. Pre-existing CPU conversion warnings
  remain; no new audio compiler warnings were emitted.
- Complete SDL-disabled, trace-disabled Debug CTest: **14/14 passed**.
- All 11 individual cpu_instrs, instr_timing and three individual mem_timing
  ROMs: **15/15 passed**. Logs: /tmp/gb-background-regressions/audio/.
- SDL audio-diagnostic smoke with dummy/software drivers: presented 220 frames
  with audio enabled; a separate muted run presented three frames. Both exited
  successfully. Regeneration reproduced audio.gb byte-for-byte; git diff --check
  passed.
- Upstream dmg_sound singles are **not runnable with current cartridge support**:
  attempted 01-registers.gb and confirmed loader rejection of type 03 with 8 KiB
  cartridge RAM. Inspected all twelve singles' headers; each uses that same
  unsupported configuration. No hardware sound-suite pass is claimed and the
  cartridge tests/loader were not weakened.

### Remaining limitations

Wave RAM access is unrestricted rather than modeling DMG active-wave windows,
redirected accesses or retrigger corruption. Pulse first-sample and exact wave
fetch/trigger phases, simultaneous register races, model-specific envelope and
power quirks remain simplified. Mixing/filtering is approximate and not
band-limited; VIN, CGB/GBA audio, adaptive audio clock synchronization, host
volume controls and recording are absent. Host stalls may cause gaps; samples
are bounded rather than allowing growing latency. Tetris audio and real speaker
quality require manual verification. Earlier Tetris launch evidence is unchanged.

## 2026-10-29 — Tetris played with sound

- The user confirmed playing Tetris with sound in the emulator. This extends
  the earlier launch-only result to manual gameplay and audible-output evidence
  for the user's tested ROM.
- Updated the current status, README compatibility section and AUDIO.md.
  Earlier entries retain the verification scope known at the time.
- Documentation-only change: no emulator code changed, no ROM was rerun by the
  agent and no automated tests were rerun. git diff --check passed.
- ROM revision/checksum, exact sound fidelity, full-game completion and
  long-session stability were not reported as verified.

## 2026-10-29 — Audio source comments and API explanations

- Expanded comments across include/apu.h, the private audio interface, register
  handling, channel sequencing, PCM mixer and SDL audio header/implementation.
- Explained channel state and counter units, register masks/block offsets,
  DAC versus generator activity, trigger/sweep side effects, divider bit 12 and
  the eight sequencer steps. Explained wave nibble order, noise feedback,
  duty masks, stereo routing, gain/scaling, rational sample cadence, high-pass
  filter state, PCM frame/byte counts, ring overflow and SDL resource ownership.
- Marked existing approximations where relevant; no audio behavior or timing
  changed. Compared lexical C tokens before/after while excluding comments:
  all seven edited source/header files matched exactly.
- Rebuilt successfully and reran APU_Tests, Audio_Diagnostic_ROM and
  SDL_Audio_Tests: **3/3 passed**. git diff --check passed. Full regressions and
  sanitizer suites were not rerun for this comment-only task; previous results
  remain recorded in their original entries.

## 2026-10-29 — Standard MBC1 banking, RAM and battery saves

### Implementation

- Added standard MBC1 types 01/02/03 alongside ROM-only type 00. Supports
  power-of-two ROMs up to 2 MiB, both banking modes, low-five-bit zero-bank
  translation before physical size masking, secondary bank bits and mode-dependent
  lower-window/RAM selection. Mapper writes never overwrite ROM or add time.
- Added gated external RAM: low-nibble A enables it, other values disable it;
  mode 1 selects four 8 KiB banks where fitted. Small RAM chips mirror missing
  address lines. Rejects impossible large-ROM + 32 KiB RAM configurations,
  inconsistent RAM-bearing headers and unsupported mapper families.
- Kept mapper translation in private src/mbc1.c, loader/dispatch in cartridge.c,
  and persistence in cartridge_save.c. Existing memory bus and DMA raw master
  already route through the cartridge interface; CPU/PPU/APU timing is unchanged.
- Preserved the inline two-bank ROM for existing fixtures; larger images use
  owned heap storage. Added cartridge_destroy()/memory_destroy(), replacement
  cleanup, and runner shutdown/startup-error cleanup. Failed replacements preserve
  the old image, RAM and mapper state. Cartridge/Memory owners must not be copied.
- Loader now checks exact declared ROM length rather than rejecting all files
  above 32 KiB. Common repeated-logo 1 MiB MBC1M images are explicitly rejected;
  that detection is heuristic and MBC1M remains unsupported.
- SDL loads type 03 battery RAM from ROM-path + .sav, then saves on clean exit.
  Missing saves start fresh; malformed existing saves stop startup. Writes use
  a checked temporary file and rename. Failed sessions do not overwrite saves.
  Headless runs do not implicitly access save files; explicit core import/export
  APIs are available. Type 02 RAM remains volatile.
- Added MBC1.md covering board limits, registers, worked bank examples, diagram,
  ownership/lifetime, file policy and validation. Updated README, SDL, PPU and
  audio docs to describe current capabilities while preserving historical entries.

### Verification performed

- New MBC1_Tests sweeps all byte values for low/secondary bank latches across
  both modes on a 2 MiB ROM; verifies small-ROM masking, both ROM windows,
  bank-zero translation, RAM banks/enable/mirroring, fixed large-ROM RAM,
  battery save roundtrips, invalid file/header/save preservation, multicart
  rejection and repeatable destruction. The multicart case was added after the
  full runs and the MBC1 target was rebuilt/rerun in all three configurations.
- Executes CPU instructions that switch banks and jump into selected ROM;
  checks exact CPU/divider cycles. DMA reads selected ROM and enabled/disabled
  cartridge RAM, while CPU writes during DMA remain denied.
- Updated the old fixed-mapping assertion: writing bank 2 on a two-bank MBC1
  chip now correctly mirrors physical bank 0. Existing invalid-load/parser
  coverage and rejection of RAM-bearing types with zero declared RAM remain.
- Complete SDL + trace Debug CTest: **22/22 passed**.
- Complete SDL + trace ASan/UBSan CTest: **22/22 passed**; no sanitizer diagnostics.
  ASAN_OPTIONS=detect_leaks=0 retains the existing ptrace/LeakSanitizer workaround.
  Existing CPU conversion warnings remain; no new mapper warnings were emitted.
- Complete headless, trace-disabled Debug CTest: **15/15 passed**.
- All 11 individual cpu_instrs, instr_timing and three individual mem_timing
  ROMs: **15/15 passed**. Logs: /tmp/gb-background-regressions/mbc1/.
- SDL battery smoke used a synthetic 64 KiB RAM-bearing diagnostic. A Debug
  launch read byte 31, CPU incremented it to 32, and exit persisted it; a second
  ASan/UBSan launch read 32 and persisted 33. Both presented two frames with
  dummy/software drivers. A malformed save was rejected without overwriting it.
  Fixtures were confined to /tmp/gb-mbc1-sdl-smoke/.
- All twelve previously loader-blocked dmg_sound singles now execute with valid
  DE B0 61 result signatures. **9/12 pass** (01–08 and 11). **09-wave read while
  on, 10-wave trigger while on and 12-wave write while on fail**, each status 01.
  These expose the documented active-wave APU approximations; APU implementation
  was not changed in this mapper task. Logs/results:
  /tmp/gb-mbc1-sound-regressions/.
- git diff --check passed. No CTest failures; three external sound ROM failures
  remain explicitly recorded above.

### Limits

Only standard MBC1 and ROM-only boards are implemented. MBC1M, MBC2, MBC3/RTC,
MBC5 and nonstandard boards are unsupported. Mapper validation uses synthetic
bank fixtures and CPU/audio ROMs rather than a dedicated mapper hardware
acceptance suite or physical cartridges. Persistence is a raw RAM snapshot,
not a save state: no autosave, concurrent-instance locking, power-loss durability
or forced-termination recovery is promised. The existing DMG timing/graphics
and wave-channel approximations remain unchanged. Tetris's earlier user-reported
play-with-sound milestone is preserved; no new manual game compatibility claim
is inferred from this implementation.

## 2026-10-30 — Wave investigation: fresh baseline and initial implementation

- Read Pan Docs Audio Registers/Audio Details (upstream Markdown), bundled
  blargg dmg_sound sources 09/10/12, SameSuite CH3 delay test and SameBoy's
  hardware observations. DMG active RAM accesses share the selected fetch byte;
  other reads return FF and writes are ignored. Retrigger corruption uses the
  upcoming byte, copying byte 0 or an aligned four-byte block. SameBoy records
  physical DMG-B unit variation; the target is the common blargg behavior.
- Before edits, freshly configured/built SDL + graphics-trace Debug in
  /tmp/gb-wave-debug: all 22 CTests passed. Reran all twelve dmg_sound singles:
  01–08 and 11 passed, 09/10/12 failed status 01; every result had DE B0 61.
  These are current baseline results, independently rerun from earlier entries.
  Logs: /tmp/gb-wave-results/baseline/.
- Added DMG fetch-window RAM arbitration, upcoming-fetch retrigger corruption,
  six-T-cycle wave startup latency and inactive CH3 sample retention. Updated
  the old immediate-start timing assertion to check the startup delay. Further
  focused tests and post-change ROM/regression checks are pending.

## 2026-10-30 — Initial wave fix verification and regression coverage

- Fresh post-change run of all twelve dmg_sound singles using the headless
  runner: **12/12 pass**, status 00 and DE B0 61 for every ROM, including
  formerly failing 09/10/12. Logs: /tmp/gb-wave-results/after/.
- Reran existing SDL + trace Debug CTests after the behavioral change:
  **22/22 passed**. No previous CTest was removed or disabled. The existing
  wave-period assertion now includes the documented startup latency; its
  subsequent two-cycle sample-period check remains intact.
- Added APU_Wave_Tests covering all 16 CPU addresses at all 32 sample positions,
  both T-cycles of the access window and rejection after it, ignored writes,
  retained output on accepted RAM writes/retrigger/disable, all eight retrigger
  phases at every position (including wraparound), the disable-before-trigger
  workaround, inactive DAC-on access, length expiry, power-off and batch equality.
- Added a signature/status-checking sound ROM runner and CTest registration of
  every bundled dmg_sound single. Missing results/timeouts/failures return a
  failing process status. Full expanded Debug and sanitizer verification pending.

## 2026-10-30 — Expanded checks and ROM runner startup correction

- Expanded Debug, SDL + trace ASan/UBSan and headless CTests initially reported
  35/35, 35/35 and (headless run in progress) respectively. Inspection revealed
  a runner issue: blargg writes DE B0 61 before setting status 80, so these
  initial new DMG_Sound CTest passes could accept initial RAM zero too early.
  They are superseded by corrected-runner results below, not counted as ROM
  verification. The independent full-duration headless ROM runs above remain
  valid; their terminal results were collected after ten million instructions.
- Corrected the runner to require signed status 80 before accepting any terminal
  result. Existing CTests and the focused APU_Wave_Tests passed in the initial
  expanded runs; all configurations are being rebuilt/rerun with this correction.
- Independently reran all 11 cpu_instrs singles, instr_timing and three
  mem_timing singles: **15/15 passed**, using full-duration headless execution.
  Logs: /tmp/gb-wave-results/cpu-timing/.
- Updated AUDIO.md with per-ROM before/after results, exact DMG wave behavior,
  reference links, physical-unit caveats and the CGB-only scope of SameSuite's
  PCM hardware checks. Updated MBC1.md to distinguish its historical 9/12 result
  from the subsequent wave fix. No physical-console or SameSuite execution is
  claimed. Final corrected CTest results and diff checks pending.

## 2026-10-30 — Final active-wave regression results

- With the corrected runner requiring signed running status 80, freshly rebuilt
  and reran the full SDL + trace Debug suite: **35/35 passed** (all original
  22 tests, the focused wave test and twelve sound ROM tests).
- Rebuilt/reran SDL + trace ASan/UBSan: **35/35 passed**, no sanitizer diagnostics.
  ASAN_OPTIONS=detect_leaks=0 retains the earlier environment's LeakSanitizer
  workaround; leak checking is not claimed. Existing CPU conversion warnings
  remain; no new wave-related compiler warnings were emitted.
- Rebuilt/reran headless, trace-disabled Debug: **28/28 passed** (the original
  15 tests plus wave coverage and twelve sound singles).
- Every dmg_sound single passed with status **00**, signature **DE B0 61**:
  01-registers; 02-len ctr; 03-trigger; 04-sweep; 05-sweep details;
  06-overflow on trigger; 07-len sweep period sync; 08-len ctr during power;
  09-wave read while on; 10-wave trigger while on; 11-regs after power;
  12-wave write while on. The previous nine passes are preserved and the
  previous three failures are fixed. AUDIO.md contains the per-ROM comparison.
- Verified the ROM runner itself against temporary synthetic cartridges: signed
  running-to-failure returns exit 1; signed running-to-pass returns exit 0;
  signature plus startup zero without running returns exit 1 on STOP.
  Logs/fixtures: /tmp/gb-wave-results/runner-fixtures/.
- The independent CPU/timing ROM rerun remains **15/15 passed** as recorded
  above. No external ROMs were modified. No tests were removed or disabled.
- git diff --check passed. Complete logs are in /tmp/gb-wave-results/;
  ctest-debug.log, ctest-sanitize.log and ctest-headless.log are the corrected
  final CTest runs. Earlier unrelated workspace edits were preserved.
- Limits: common deterministic DMG-B corruption behavior only; physical unit
  variations, CGB/AGB modes, inactive CH3 bus glitches and simultaneous bus/clock
  races remain outside this fix. SameSuite sources were consulted, not run;
  PCM tests verify CGB hardware, not DMG windows. No physical audio fidelity,
  new gameplay or listening verification is claimed.

## 2026-10-30 — Mortal Kombat investigation: reproduction and tracing

- Found roms/Mortal_Kombat.gb, 262144 bytes, SHA-256
  84686cd3c77c533926fc2c56783613ecc5781de1bff7065e00112a69d17a5e69.
  Header uses supported MBC1 type 01, 256 KiB ROM, no cartridge RAM.
- Unmodified core ran ten million steps and remained RUNNING around 0E24/0E28;
  no STOP/lock. ROM disassembly shows a polling loop on WRAM DFC1 after writing
  SC=81. Serial vector 0058 jumps to 0276, whose handler reads SB and sets DFC1.
  The bus currently only prints SB on SC=81; it never shifts bits, clears SC
  start, or requests the serial interrupt. This is evidence for a missing
  serial-completion condition, not a mapper-capacity problem.
- Added opt-in headless bounded CPU snapshots (up to 4096 + final snapshot),
  start/stride/count controls and bounded --steps. Records requested CPU/IRQ/PPU
  and MBC1 bank fields plus timer/serial registers and framebuffer hashes.
  Frame totals/hash-change totals help separate CPU polling from display stalls.
  Initial diagnostic build succeeded. Baseline/focused traces are in
  /tmp/gb-mk-results/. Exact trace analysis, regression tests and fix pending.

## 2026-10-30 — Mortal Kombat diagnosis and serial completion fix

- Focused pre-fix trace at step 4,000,000 shows repeated valid instructions:
  0E24 FA C1 DF (LD A,[DFC1]); 0E27 A7 (AND A); 0E28 CA 24 0E
  (JP Z,0E24). SC remains 81, SB AA, IME=1, IE=0D, IF=00, SP=DFF0,
  bank=1. LY/STAT/DIV advance and bank 2 sound-handler excursions occur.
  Final baseline: 115240128 T-cycles, 1636 frames, 37 changed frames,
  framebuffer hash 45652487. CPU polls; it is not HALTed/STOPped/locked.
- Serial ISR at 0058 jumps to 0276: PUSH AF; LD A,[FF01]; LD [DFC0],A;
  LD A,1; LD [DFC1],A; POP AF; RETI. No serial IF request ever reaches it
  because the old FF02 handler is only an immediate stdout byte print.
- Compared Pan Docs Serial Data Transfer with SameBoy Core/timing.c
  GB_serial_master_edge: both complete eight bit shifts, clear SC.7, request
  IF.3 and shift in ones without a cable. No installed reference emulator was
  found; this is a source/hardware-reference comparison, not a side-by-side
  game execution comparison.
  Sources: https://gbdev.io/pandocs/Serial_Data_Transfer_(Link_Cable).html
  and https://github.com/LIJI32/SameBoy/blob/master/Core/timing.c .
- Added Serial_Tests, first running its synthetic IRQ-flag polling program
  against the old core: it failed the progress marker assertion as expected
  (/tmp/gb-mk-results/serial-red.log). The test does not need retail ROM data.
- Implemented disconnected DMG serial peripheral: 512 T-cycles per bit,
  partial SB shifting, eight-bit completion, SC masks/cancel/restart/external
  wait, IF.3 regardless of IME/IE. Uses memory_tick without extra CPU time;
  keeps the test-ROM stdout sink at completion instead of transfer start.
  Polling, HALT/IME on/off, STOP, DMA, byte timing, no repeated completion,
  external waiting, cancellation, unrelated IF bits and batching tests pass.
- Core CPU/timer/PPU/mapper logic is unchanged. Serial startup currently waits
  one full bit period; free-running divider alignment and link hardware races
  are not modeled. No cable peer or CGB fast serial support is claimed.
  Game post-fix analysis and full regression runs are pending.

## 2026-10-30 — Mortal Kombat progress and main regressions verified

- Post-fix ten-million-step run: CPU remains RUNNING, reaches multiple screen
  states, 1524 VBlanks and 69 changed-frame hashes (baseline: 1636 and 37).
  A temporary observer counted 410 visits to the serial vector and no HALT,
  STOP or lock states in that run. Final framebuffer hash is 5B93C0C8.
- Exported diagnostic framebuffer captures using a /tmp observer; visually
  inspected frame 600 (MIDWAY PRESENTS) and frame 900 (MORTAL KOMBAT title).
  This verifies departure from the reported splash in the actual provided ROM,
  not merely a changed hash. Captures/logs: /tmp/gb-mk-results/mk-*.png and
  probe.log. This was not manual interactive combat or full-game completion.
- Full SDL + trace Debug CTest: **36/36 passed**, including all twelve sound
  singles and the new serial regression. Full SDL + trace ASan/UBSan CTest:
  **36/36 passed**, no sanitizer diagnostics, ASAN_OPTIONS=detect_leaks=0 as in
  prior runs. Existing CPU conversion warnings remain.
- All 11 cpu_instrs singles, instr_timing and three mem_timing singles were
  rerun with completed serial output: **15/15 passed**, requiring process
  success, serial Passed and no Failed. Logs: /tmp/gb-mk-results/cpu-timing/.
- Updated README with serial behavior, tracing flags, meanings and bounds,
  diagnosis and remaining timing uncertainty. Headless CTest and additional
  SDL/CLI/save checks are being finalized; their results are not claimed here.

## 2026-10-30 — Mortal Kombat final preservation checks

- Headless, graphics-trace-disabled Debug CTest: **29/29 passed**. All existing
  tests remain enabled. SDL + trace suites above passed **36/36** in Debug and
  ASan/UBSan. All twelve sound tests report status 00 and DE B0 61:
  01-registers; 02-len ctr; 03-trigger; 04-sweep; 05-sweep details;
  06-overflow on trigger; 07-len sweep period sync; 08-len ctr during power;
  09-wave read while on; 10-wave trigger while on; 11-regs after power;
  12-wave write while on. The previous 15/15 CPU/timing rerun is recorded above.
- SDL dummy video/software renderer/audio runs exited successfully with audio
  enabled: Tetris presented 120 frames; Mortal Kombat presented 950 frames.
  This checks launch/render/audio integration, not interactive play or listening.
- Rechecked SDL MBC1 battery saves using an isolated synthetic type-03 ROM and
  /tmp save: Debug loaded byte 31 and persisted 32; ASan/UBSan loaded 32 and
  persisted 33. Both presented two frames. Existing MBC1_Tests also passed
  save roundtrips/invalid-save preservation. No user save files were touched.
- Diagnostic CLI checks passed: start/stride/count produce three selected
  snapshots plus one final snapshot; traced/untraced 200-step CPU states match;
  zero counts/stride/steps, negative steps, missing values, excessive count
  (4097), and unknown options fail cleanly. Retained README's --trace spelling
  as an alias of --cpu-trace. CPU tracing requires no graphics-trace build flag.
- git diff --check passed. Updated standard build/ binaries are being rebuilt
  for the user's next run. Earlier unrelated workspace changes remain intact.
- Remaining uncertainty: no physical console or separate emulator executable
  was run, and no interactive Tetris/Mortal Kombat combat or audio listening
  session was performed. Hardware behavior was compared with Pan Docs and
  SameBoy source. Serial divider startup phase, active-transfer register races,
  a connected cable, external clock input, and CGB fast serial are unsupported.
  Existing PPU/analog/STOP approximations remain. No game-specific branch,
  mapper exception, forced interrupt or WRAM flag patch was introduced.

## 2026-10-30 — Workspace build ready

- Standard build/ was initially configured headless: rebuilt it and reran
  **29/29 CTests successfully**. Enabled its SDL option and rebuilt again so
  build/gb_sdl includes the serial fix instead of leaving an older executable.
  The resulting SDL, trace-disabled workspace suite passed **34/34**.
- The separate SDL + trace configurations retain the **36/36** Debug and
  ASan/UBSan results above; the headless configuration passed **29/29**.
  Logs: /tmp/gb-mk-results/ctest-workspace.log and the other ctest-*.log files.
  No requested verification remains outstanding. git diff --check passed.
