# Emulator progress

Append a dated entry after each meaningful implementation or verification task.
Record what changed, what was actually tested, results, and remaining limitations.
Keep earlier entries; distinguish previous results from tests rerun today.
Dates use Africa/Dar_es_Salaam time.

## Current status

- User-reported manual compatibility milestone: `Tetris.gb` successfully opened
  on 2026-10-29; gameplay and ROM revision remain unverified.

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
- Cartridge support remains limited to fixed 32 KiB mapping without external RAM.
- The cartridge loader defect has been fixed by rejecting unsupported RAM-bearing
  mapper types. The twelve headless suites pass; SDL + trace suites also pass (18/18 in Debug
  and ASan/UBSan);
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
