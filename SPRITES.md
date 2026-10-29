# DMG sprites: OAM selection and pixel composition

`memory_init()` binds the PPU to bus-owned OAM and the DMA-active flag using
`ppu_attach_oam()`. Sprite selection now reads the same 160-byte OAM array that
CPU writes and progressive DMA update. The PPU uses a const device view, so its
reads bypass CPU bus grants without generating CPU instructions or extra time.

## Mode 2 selection

Mode 2 visits OAM entries 0–39 at dots 2,4,…,80. Each four-byte entry contains
Y, X, tile and flags. A Y-overlap match appends a value snapshot to
`PPU.sprites[10]`, up to ten entries. The array retains OAM order; it is not sorted
before applying the limit. Offscreen X coordinates consume selection slots.
Selection state resets at each new scanline and LCD enable/disable transition.

The screen origin is `(OAM_X-8, OAM_Y-16)`, calculated with signed integers to
handle partial top/left clipping. LCDC bit 2 selects height 8 or 16. In 8×16
mode, the OAM tile number's low bit is ignored, and vertical flipping mirrors
both tiles as one 16-pixel object. OBJ data always uses unsigned tiles at 8000,
independently of the BG/Window tile-addressing bit. These rules and the DMG
priority order follow [Pan Docs: OAM](https://github.com/gbdev/pandocs/blob/master/src/OAM.md).

This implementation snapshots all four attributes and the object height when
the entry is scanned. It does not reproduce the hardware's separate OAM/fetch
phases for each attribute (see inaccuracies below).

## Rendering and priority

At the existing dot-252 row commit, selected sprite rows are decoded once with
`ppu_decode_tile_row()`. For each screen pixel, the compositor:

1. Computes the BG/Window raw color index and BGP-mapped shade as before. BG
   disable makes the raw index zero and the background white; sprites remain
   independently controlled by LCDC bit 1.
2. Looks at selected objects covering that X coordinate, applying flag bit 5
   (X flip) and bit 6 (Y flip). Raw OBJ color zero is transparent before palette
   mapping, regardless of OBP's low two bits.
3. Chooses the first opaque OBJ by DMG priority: smaller OAM X, then earlier OAM
   index. Palette-mapped white is still opaque. The behind-BG flag does not
   participate in this winner selection.
4. Applies the winner's flag bit 7. When set, a nonzero BG/Window **color index**
   hides that object. A zero index permits it. Testing a final grayscale shade
   would be wrong: BGP can map zero to black or a nonzero color to white.
5. Maps a visible OBJ through OBP0 or OBP1 (flag bit 4), then stores exactly one
   final shade in the existing framebuffer. CGB-only flag bits are ignored.

A winner hidden by BG still masks lower-priority objects; the compositor does
not fall through to them. An object with transparent color zero does allow
lower objects through. Window pixels use their own original color index in the
same priority comparison. Sprites do not affect the Window's internal counter.

## Functions and state

| Function/state | Responsibility |
| --- | --- |
| `ppu_attach_oam()` | Binds borrowed OAM/DMA views. NULL OAM clears selection; non-NULL rebinding preserves value snapshots |
| `scan_sprite()` | Checks one OAM entry's Y range during Mode 2 and snapshots it if a slot remains |
| `oam_dma_owns_bus()` | Reads the optional borrowed DMA ownership flag |
| `decode_sprite_rows()` | Prepares up to ten decoded rows using unsigned tiles, 8×16 pairing and whole-object Y flip |
| `sprite_pixel()` | Selects the winning nontransparent OBJ at one X, using DMG X/OAM priority |
| `render_scanline()` | Composes BG, Window and objects, tests raw background color priority, maps palettes and commits final pixels |
| `PPUSprite` | Y/X/tile/flags/OAM index/height snapshot; no borrowed per-entry pointers |
| `sprite_count` | Number of selected entries on the current line, bounded by ten |

`render_scanline()` replaces the private `render_background_line()` name to
reflect the combined compositor. No public CPU or timing API changed. SDL
already presents this shared framebuffer, so it needs no sprite-specific code.

Like VRAM, OAM storage and the DMA flag must outlive the PPU. After copying or
moving Memory, rebind both views:

```c
ppu_attach_vram(&memory.ppu, memory.vram);
ppu_attach_oam(&memory.ppu, memory.oam, &memory.dma.active);
```

Non-NULL rebinding retains snapshots already captured for an in-progress line.
Standalone PPU instances without OAM simply have no sprite selection.

## DMA and remaining timing approximations

Mode lengths remain unchanged: Mode 2 is 80 dots, Mode 3 is 172 dots, and the
whole composed row appears at dot 252. Sprite fetch stalls and FIFO timing are
not implemented. [Pan Docs' rendering overview](https://github.com/gbdev/pandocs/blob/master/src/Rendering.md)
describes variable transfer timing that this fixed-mode model does not emulate.

During active DMA, Mode 2 skips entries whose scan occurs while OAM is owned by
DMA. If DMA is active at the row commit, object drawing is suppressed for that
row. This is a deterministic approximation: it does not reproduce mixed OAM
words, partially corrupted tile/attribute fetches or revision-specific artifacts.
Objects selected before a DMA starts can therefore be hidden at commit. Once
DMA finishes, subsequent scans see the transferred OAM bytes.

Other limitations remain explicit:

- All four OAM attributes and size are captured together per entry; real read
  phases and mid-scanline OAM/LCDC-size effects can differ.
- Tile bytes, palettes and enable state are sampled at row commit, rather than
  individual sprite fetch phases. No pixel-level palette/LCDC raster effects.
- The renderer implements DMG priority only, not CGB OAM-order-only priority,
  CGB tile banks or color palettes.
- Existing LCD startup, STAT-write, line-153 LY, Window WX quirks and DMA source-
  arbitration gaps remain. No APU, additional mapper or pixel FIFO was added.

## Tests and graphics trace

`PPU_Sprite_Tests` is registered with CTest. It covers:

- Two-dot scan progress, the last OAM entry at dot 80, attribute snapshots,
  rejected CPU writes during Mode 2 and the first-ten Y-match selection limit.
- Offscreen objects consuming slots, Y-hidden entries freeing slots, reset,
  detached OAM and pointer rebinding after a Memory copy.
- 224 full-frame fixture combinations of sizes, X/Y flips, palettes, BG tile-
  addressing mode and clipped/hidden positions. Each pixel is compared against
  an independent geometry/palette reference, including highest tile IDs and
  odd IDs in 8×16 mode.
- Color-zero transparency, opaque white pixels, OBP choice, ignored CGB flags,
  smaller-X priority, OAM ties and behind-BG masking of lower objects.
- BG/Window color-ID priority, BG disable leaving objects enabled, OBJ disable,
  progressive DMA-to-OAM-to-pixel integration and shared-clock batching.

With graphics tracing compiled in, a sprite-visible selected pixel produces
one final `PIXEL layer=sprite` record with OAM address/index, original OAM tile
and flags, actual row address/bytes, color, OBP byte and final shade. The row
address identifies the effective tile/row after 8×16 pairing and Y flip.
A BG/Window-hidden winner leaves the final pixel trace on the visible lower
layer; no false sprite overwrite is logged. SDL logs the same final shade as
before. The bounded trace does not log every candidate or every OAM scan.

```sh
cmake --build build -j2
ctest --test-dir build --output-on-failure
ctest --test-dir build --output-on-failure -R '^PPU_Sprite_Tests$'
```

Current regression results are in [PROGRESS.md](PROGRESS.md). No external hardware
sprite acceptance suite or physical DMG was tested in this iteration.
