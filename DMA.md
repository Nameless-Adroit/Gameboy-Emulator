# DMG OAM DMA and CPU bus access

FF46 starts a progressive copy from `page*100 .. page*100+9F` into OAM
`FE00..FE9F`. The new `DMA` state lives in `Memory`; `src/dma.c` handles the
transfer, and the existing `memory_tick()` supplies its elapsed time.

No host memcpy performs this transfer. Each byte reads the current bus value
and is written only when its four-T-cycle slot completes. Mutating a future
source byte before its slot therefore changes the transferred byte. DMA does
not add CPU cycles, stall the CPU globally or create another hardware clock.

## Transfer timing used by this model

A raw or CPU-accepted FF46 write records its page immediately and schedules a
start eight T-cycles later. An existing transfer continues during the pending
restart. At the scheduled start, source/index/phase reset; existing OAM contents
remain until overwritten progressively. Another FF46 write during startup
replaces the pending request and starts a fresh delay.

Time is measured from the FF46 write's existing end-of-access sampling point:

| Elapsed T-cycles | State |
| --- | --- |
| 0 | Request queued; no byte copied and no clock advanced by the write itself |
| 4 | Fresh request still in startup; ordinary PPU-mode access rules apply |
| 8 | New transfer active; DMG CPU bus restriction takes effect; index=0 |
| 12 | First byte committed to FE00 |
| 16 | Second byte committed to FE01 |
| 644 | 159 bytes complete, DMA still active |
| 648 | Last byte committed to FE9F, DMA becomes inactive |

There are **640 active transfer T-cycles plus an eight-cycle modeled startup**.
The distinction matters: the whole sampled-write-to-completion interval is 648,
not 640. This uses the emulator's machine-cycle access convention, not a claim
that all DMA signal phases have been reproduced. The startup/restart distinction
and adjacent completion checks are informed by the hardware-tested
[Mooneye startup](https://github.com/Gekkio/mooneye-test-suite/blob/main/acceptance/oam_dma_start.s),
[timing](https://github.com/Gekkio/mooneye-test-suite/blob/main/acceptance/oam_dma_timing.s)
and [restart](https://github.com/Gekkio/mooneye-test-suite/blob/main/acceptance/oam_dma_restart.s)
tests. Those external ROMs were not run; local tests explicitly verify this
schedule and its CPU sampling boundaries.

[Pan Docs](https://github.com/gbdev/pandocs/blob/master/src/OAM_DMA_Transfer.md)
describes the 160-M-cycle payload transfer, DMG HRAM restriction and FF46 source
range. Valid modeled source pages are 00..DF, using the existing cartridge/VRAM/
external-RAM/WRAM mapping. Absent cartridge RAM reads FF as before. Pages E0..FF
still consume transfer time but supply FF deterministically; undocumented high-
page mirroring and hardware-dependent bus values are not implemented.

## Raw access versus CPU access

The memory bus now has distinct untimed entry points:

| Function | Purpose |
| --- | --- |
| `memory_read()` / `memory_write()` | Raw mapped access for debugger/test fixtures/devices; no CPU restrictions or implicit clock ticks |
| `memory_cpu_read()` / `memory_cpu_write()` | Apply CPU ownership/access policy, then use the raw bus if granted; still do not advance time |
| `memory_dma_read()` | DMA source read through the raw mapping, bypassing CPU grants and CPU graphics-trace records |
| `cpu_read()` / `cpu_write()` (private CPU helpers) | Advance the existing four-T-cycle access, then sample the CPU-facing entry point |

Opcode fetches, operands, indirect reads/writes and stack operations already use
those timed CPU helpers, so all get the new policy. Blocked reads return FF and
blocked writes are ignored. Access is checked at the end of the CPU machine
cycle, after devices have advanced to that sample point. No instruction timing
tables or PPU mode lengths changed.

While DMA is active, CPU access is restricted to **FF80..FFFE HRAM**. FFFF (IE)
is outside that range. FF46 writes are a deliberate exception permitting restart;
CPU FF46 reads remain blocked. A routine executing in ROM does not magically
pause: it fetches FF on the blocked bus, which is RST 38. ROM, WRAM, VRAM, OAM,
I/O and non-HRAM stack accesses are all blocked during active DMA.

Internal interrupt-controller sampling/acknowledgment of IF/IE remains raw. It
is not a CPU data bus transaction. Interrupt requests continue to be generated;
DMA does not disable IME automatically. Software normally executes a DMA wait
routine in HRAM with interrupts disabled because handler code in ROM and a stack
outside HRAM will be inaccessible.

Independently of DMA, CPU LCD-mode restrictions now apply:

| LCD state/mode | CPU VRAM | CPU OAM |
| --- | --- | --- |
| LCD off | Allowed | Allowed |
| Mode 0 HBlank | Allowed | Allowed |
| Mode 1 VBlank | Allowed | Allowed |
| Mode 2 OAM scan | Allowed | Blocked |
| Mode 3 transfer | Blocked | Blocked |

DMA's HRAM rule takes precedence. The PPU keeps its direct read-only VRAM view;
DMA writes OAM directly as a hardware bus master. These accesses are not blocked
by CPU grants. The basic VRAM/OAM rules follow
[Pan Docs' access description](https://github.com/gbdev/pandocs/blob/master/src/Accessing_VRAM_and_OAM.md).

## Function and clock responsibilities

- `dma_start()` queues the requested page and startup delay. It does not clear
  OAM or immediately stop an earlier transfer.
- `dma_tick()` advances pending/active state and commits one byte per four active
  T-cycles. It reads through `memory_dma_read()` and writes only the destination
  byte; no bus read/write or DMA function calls `memory_tick()` recursively.
- `memory_init()` zero-initializes DMA together with other device state. CPU-only
  reset retains its existing separation from peripheral reset.
- `memory_write()` owns FF46. `ppu_read()`/`ppu_write()` still do not own FF46.
- `cpu_access_blocked()` is a private policy shared by CPU reads and writes.
- `tick_devices()` advances timer, PPU and DMA using the same duration, then
  updates an attached trace observer's cycle count once.
- `memory_tick()` interleaves all three devices one T-cycle at a time while a
  DMA request/transfer is pending. When DMA ends inside a large batch, it resumes
  ordinary batched ticking for the remainder. Normal non-DMA execution keeps
  the batched path. Zero-cycle calls retain pending PPU IRQ flushing semantics.

Graphics trace does not report DMA reads as CPU ROM data reads, even when DMA
progress occurs inside a CPU step. Blocked CPU accesses do not reach raw bus
trace hooks; existing graphics write records represent accesses accepted by the
bus policy (which may still be ignored by a read-only register).

## Transfer and restriction tests

`DMA_Tests` runs in CTest and checks:

- Startup, no implicit copy/tick, first-byte timing, each byte boundary, last-byte
  completion and no writes after completion.
- Live source reads; ROM, VRAM, WRAM and absent/unsupported-source behavior.
- Restart while active, old-copy progress during delay and latest pending page.
- Batch/single-T-cycle equivalence with timer/PPU state preserved.
- HRAM access, ignored non-HRAM writes, blocked reads, FF46 restart exception,
  raw fixture access and LCD mode-dependent VRAM/OAM access.
- Real HRAM CPU code that starts DMA and waits; blocked data reads/writes and
  non-HRAM stack writes; blocked opcode fetches producing RST 38.
- CPU OAM reads sampled at 644 (blocked) versus 648 (complete), plus CPU VRAM
  reads spanning mode-3 start/end boundaries without altered cycle counts.
- DMA during HALT while timer and VBlank both request interrupts, using one clock.
- With graphics tracing compiled in, no fabricated CPU ROM reads from DMA.

```sh
cmake --build build -j2
ctest --test-dir build --output-on-failure
ctest --test-dir build --output-on-failure -R '^DMA_Tests$'
```

## Remaining inaccuracies

This is a DMG machine-cycle transfer model. Startup/restart sub-cycle phases,
source-bus/open-bus conflicts and undocumented high-page source decoding are
simplified. DMA source VRAM currently uses the raw map even in PPU mode 3;
revision-specific source arbitration is not modeled. Sprite selection/rendering
now observes OAM and the DMA-active flag, but conflicts are approximated as hidden
objects instead of reproducing sprite/OAM word corruption. CGB separate buses,
double speed and HDMA are not implemented. PPU blocking follows its existing
fixed mode-3 boundaries, not a pixel-FIFO model. CPU accesses sample at the end of
each machine cycle, not at every real bus phase.

See [PROGRESS.md](PROGRESS.md) for current regression results and environment
limits. No dedicated external DMA hardware-acceptance ROM suite was run.
