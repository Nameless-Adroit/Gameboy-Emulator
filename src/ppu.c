#include "ppu.h"
#include "graphics_trace.h"

#include <string.h>

/* Scanline background renderer with fixed timing. Mode 3 uses 172 dots;
 * pixel fetching, scroll/window/sprite stalls, LY's line-153 quirk and the
 * DMG STAT-write glitch are not implemented. VRAM/OAM remain on the bus. */
enum {
    DOTS_PER_LINE = 456,
    LINES_PER_FRAME = 154,
    OAM_SCAN_DOTS = 80,
    TRANSFER_DOTS = 172,
    LCD_ENABLE = 0x80,
    VBLANK_INTERRUPT = 0x01,
    STAT_INTERRUPT = 0x02
};

static unsigned current_mode(const PPU *ppu)
{
    if (!(ppu->lcdc & LCD_ENABLE)) return 0;
    if (ppu->ly >= LCD_HEIGHT) return 1;
    if (ppu->dot < OAM_SCAN_DOTS) return 2;
    if (ppu->dot < OAM_SCAN_DOTS + TRANSFER_DOTS) return 3;
    return 0;
}

static void update_stat(PPU *ppu)
{
    unsigned mode = current_mode(ppu);
    bool coincidence = ppu->ly == ppu->lyc;
    ppu->stat = (uint8_t)((ppu->stat & 0x78) | mode |
                         (coincidence ? 0x04 : 0));

    // All enabled conditions share one interrupt line. Overlapping sources
    // must not generate another request until that combined line goes low.
    bool line = (ppu->lcdc & LCD_ENABLE) &&
        ((coincidence && (ppu->stat & 0x40)) ||
         (mode == 0 && (ppu->stat & 0x08)) ||
         (mode == 1 && (ppu->stat & 0x10)) ||
         (mode == 2 && (ppu->stat & 0x20)));
    if (line && !ppu->stat_irq_line) ppu->pending_interrupts |= STAT_INTERRUPT;
    ppu->stat_irq_line = line;
}

void ppu_init(PPU *ppu)
{
    memset(ppu, 0, sizeof(*ppu));
    update_stat(ppu);
}

void ppu_attach_vram(PPU *ppu, const uint8_t vram[0x2000])
{
    ppu->vram = vram;
}

void ppu_attach_oam(PPU *ppu, const uint8_t oam[160], const bool *dma_active)
{
    ppu->oam = oam;
    ppu->oam_dma_active = dma_active;
    if (!oam) ppu->sprite_count = 0;
}

static bool oam_dma_owns_bus(const PPU *ppu)
{
    return ppu->oam_dma_active && *ppu->oam_dma_active;
}

// Mode 2 visits one of the forty OAM entries every two dots. Selection is based
// only on Y overlap: X-hidden objects still consume one of the ten slots.
static void scan_sprite(PPU *ppu, unsigned index)
{
    if (!ppu->oam || oam_dma_owns_bus(ppu) || ppu->sprite_count == 10) return;
    const uint8_t *entry = ppu->oam + index * 4;
    unsigned height = (ppu->lcdc & 0x04) ? 16u : 8u;
    int row = (int)ppu->ly + 16 - entry[0];
    if (row < 0 || row >= (int)height) return;
    ppu->sprites[ppu->sprite_count++] = (PPUSprite){
        entry[0], entry[1], entry[2], entry[3], (uint8_t)index, (uint8_t)height
    };
}

void ppu_decode_tile_row(uint8_t low, uint8_t high, uint8_t pixels[8])
{
    for (unsigned x = 0; x < 8; ++x) {
        unsigned bit = 7 - x;
        pixels[x] = (uint8_t)((((unsigned)low >> bit) & 1u) |
                             ((((unsigned)high >> bit) & 1u) << 1));
    }
}

void ppu_decode_tile(const uint8_t tile[16], uint8_t pixels[8][8])
{
    for (unsigned y = 0; y < 8; ++y)
        ppu_decode_tile_row(tile[y * 2], tile[y * 2 + 1], pixels[y]);
}

typedef struct {
    uint8_t pixels[8];
    unsigned offset;
} SpriteRow;

static unsigned decode_sprite_rows(const PPU *ppu, SpriteRow rows[10])
{
    // DMA conflicts are approximated as hidden objects, not OAM word corruption.
    if (!(ppu->lcdc & 0x02) || !ppu->vram || oam_dma_owns_bus(ppu)) return 0;
    for (unsigned i = 0; i < ppu->sprite_count; ++i) {
        const PPUSprite *sprite = &ppu->sprites[i];
        unsigned y = (unsigned)((int)ppu->ly + 16 - sprite->y);
        if (sprite->flags & 0x40) y = sprite->height - 1u - y;
        unsigned tile = sprite->height == 16 ? sprite->tile & 0xFEu : sprite->tile;
        // Objects always use unsigned 8000 tile addressing. y=8..15 naturally
        // selects the second tile; vertical flip mirrors the entire 16px object.
        rows[i].offset = tile * 16 + y * 2;
        ppu_decode_tile_row(ppu->vram[rows[i].offset],
                            ppu->vram[rows[i].offset + 1], rows[i].pixels);
    }
    return ppu->sprite_count;
}

// Find the first opaque OBJ pixel by DMG drawing priority. The selected list
// remains in OAM order, so a tie in X retains the earlier entry. BG priority is
// intentionally not checked here: a winning behind-BG object masks lesser OBJs.
static int sprite_pixel(const PPU *ppu, const SpriteRow rows[10], unsigned count,
                        unsigned x, unsigned *color)
{
    int winner = -1;
    for (unsigned i = 0; i < count; ++i) {
        const PPUSprite *sprite = &ppu->sprites[i];
        int local_x = (int)x + 8 - sprite->x;
        if (local_x < 0 || local_x >= 8) continue;
        unsigned pixel = (sprite->flags & 0x20) ? 7u - (unsigned)local_x : (unsigned)local_x;
        unsigned candidate = rows[i].pixels[pixel];
        if (!candidate) continue; // Color ID zero is transparent before OBP mapping.
        if (winner < 0 || sprite->x < ppu->sprites[winner].x) {
            winner = (int)i;
            *color = candidate;
        }
    }
    return winner;
}

/* Whole-row composition at fixed dot 252. Mode 2 already selected OAM entries;
 * tile data/registers are sampled here, not at hardware FIFO fetch phases.
 * Both hardware views bypass CPU access restrictions and consume no CPU time. */
static void render_scanline(PPU *ppu)
{
    uint8_t *output = ppu->framebuffer[ppu->ly];
    bool bg_enabled = (ppu->lcdc & 0x01) && ppu->vram;
    unsigned bg_map = (ppu->lcdc & 0x08) ? 0x1C00u : 0x1800u;
    unsigned bg_y = ((unsigned)ppu->scy + ppu->ly) & 0xFFu;
    int window_left = (int)ppu->wx - 7;
    bool window_visible = bg_enabled && (ppu->lcdc & 0x20) &&
                          ppu->window_y_triggered && ppu->wx <= 166;
    unsigned window_map = (ppu->lcdc & 0x40) ? 0x1C00u : 0x1800u;
    uint8_t row[8];
    SpriteRow sprite_rows[10];
    unsigned sprite_count = decode_sprite_rows(ppu, sprite_rows);
#ifdef GB_GRAPHICS_TRACE
    unsigned source_map = 0, source_id = 0, source_row = 0;
#endif
    for (unsigned x = 0; x < LCD_WIDTH; ++x) {
        bool in_window = window_visible && (int)x >= window_left;
        unsigned bg_color = 0;
        if (bg_enabled) {
            unsigned tile_x = in_window ? (unsigned)((int)x - window_left)
                                        : ((unsigned)ppu->scx + x) & 0xFFu;
            unsigned y = in_window ? ppu->window_line : bg_y;
            unsigned map = in_window ? window_map : bg_map;
            // Window starts at local x=0, or a clipped screen edge, reloading
            // the cache even when it interrupts a partially drawn BG tile.
            if (x == 0 || (tile_x & 7u) == 0) {
                uint8_t id = ppu->vram[map + (y / 8) * 32 + tile_x / 8];
                unsigned tile_offset;
                if (ppu->lcdc & 0x10) tile_offset = (unsigned)id * 16;
                else {
                    int signed_id = id < 128 ? (int)id : (int)id - 256;
                    tile_offset = (unsigned)(0x1000 + signed_id * 16);
                }
                unsigned offset = tile_offset + (y & 7u) * 2;
#ifdef GB_GRAPHICS_TRACE
                source_map = 0x8000 + map + (y / 8) * 32 + tile_x / 8;
                source_id = id;
                source_row = 0x8000 + offset;
#endif
                ppu_decode_tile_row(ppu->vram[offset], ppu->vram[offset + 1], row);
            }
            bg_color = row[tile_x & 7u];
        }
        // Keep the raw BG/Window color ID until priority is decided. Its BGP
        // shade may be white for nonzero IDs or black for zero, neither of which
        // changes the hardware's BG-over-OBJ rule. BG disable makes it white
        // and permits sprites independently of LCDC bit 0.
        unsigned shade = bg_enabled ? ((unsigned)ppu->bgp >> (bg_color * 2)) & 3u : 0;
        unsigned object_color = 0;
        int winner = sprite_count ? sprite_pixel(ppu, sprite_rows, sprite_count, x, &object_color) : -1;
        bool object_visible = winner >= 0 &&
            (!(ppu->sprites[winner].flags & 0x80) || bg_color == 0);
        unsigned object_palette = 0;
        if (object_visible) {
            object_palette = (ppu->sprites[winner].flags & 0x10) ? ppu->obp1 : ppu->obp0;
            shade = (object_palette >> (object_color * 2)) & 3u;
        }
        output[x] = (uint8_t)shade;
#ifdef GB_GRAPHICS_TRACE
        if (ppu->trace && ppu->ly == ppu->trace->y && x == ppu->trace->x) {
            if (object_visible) {
                const PPUSprite *sprite = &ppu->sprites[winner];
                unsigned offset = sprite_rows[winner].offset;
                graphics_trace_sprite_pixel(ppu->trace, sprite->index, sprite->tile,
                    sprite->flags, 0x8000 + offset, ppu->vram[offset],
                    ppu->vram[offset + 1], object_color, object_palette, output[x]);
            } else if (bg_enabled) {
                graphics_trace_pixel(ppu->trace, source_map, source_id, source_row,
                    ppu->vram[source_row - 0x8000], ppu->vram[source_row - 0x8000 + 1],
                    bg_color, ppu->bgp, output[x], in_window);
            } else {
                graphics_trace_blank(ppu->trace, ppu->vram ? "BG-disabled" : "VRAM-detached");
            }
        }
#endif
    }
    if (window_visible) ++ppu->window_line;
}

void ppu_tick(PPU *ppu, unsigned dots, uint8_t *interrupt_flags)
{
    if (ppu->lcdc & LCD_ENABLE) {
        for (unsigned i = 0; i < dots; ++i) {
#ifdef GB_GRAPHICS_TRACE
            // memory_tick commits the batch clock after ticking devices. Offset
            // within the batch gives exact event time even for a large batch.
            if (ppu->trace) ppu->trace->event_cycle = ppu->trace->cycles + i + 1;
#endif
            ++ppu->dot;
            if (ppu->ly < LCD_HEIGHT && ppu->dot <= OAM_SCAN_DOTS && (ppu->dot & 1u) == 0)
                scan_sprite(ppu, ppu->dot / 2 - 1);
            if (ppu->ly < LCD_HEIGHT &&
                ppu->dot == OAM_SCAN_DOTS + TRANSFER_DOTS)
                render_scanline(ppu);
            if (ppu->dot == DOTS_PER_LINE) {
                ppu->dot = 0;
                ppu->sprite_count = 0;
                if (++ppu->ly == LINES_PER_FRAME) {
                    ppu->ly = 0;
                    ppu->window_line = 0;
                    ppu->window_y_triggered = false;
#ifdef GB_GRAPHICS_TRACE
                    if (ppu->trace) graphics_trace_frame_start(ppu->trace);
#endif
                }
                // WY is a start-of-scanline condition, independent of SCY,
                // WX and Window enable. Later WY writes cannot unlatch it.
                if (ppu->ly < LCD_HEIGHT && ppu->ly == ppu->wy)
                    ppu->window_y_triggered = true;
                if (ppu->ly == LCD_HEIGHT) {
                    ppu->window_y_triggered = false;
                    ppu->frame_ready = true;
#ifdef GB_GRAPHICS_TRACE
                    if (ppu->trace) graphics_trace_vblank(ppu->trace);
#endif
                    ppu->pending_interrupts |= VBLANK_INTERRUPT;
                }
            }
            update_stat(ppu);
        }
    }
    *interrupt_flags |= ppu->pending_interrupts;
    ppu->pending_interrupts = 0;
}

uint8_t ppu_read(const PPU *ppu, uint16_t address)
{
    switch (address) {
    case 0xFF40: return ppu->lcdc;
    case 0xFF41: return (uint8_t)(ppu->stat | 0x80);
    case 0xFF42: return ppu->scy;
    case 0xFF43: return ppu->scx;
    case 0xFF44: return ppu->ly;
    case 0xFF45: return ppu->lyc;
    case 0xFF47: return ppu->bgp;
    case 0xFF48: return ppu->obp0;
    case 0xFF49: return ppu->obp1;
    case 0xFF4A: return ppu->wy;
    case 0xFF4B: return ppu->wx;
    default: return 0xFF;
    }
}

void ppu_write(PPU *ppu, uint16_t address, uint8_t value)
{
    switch (address) {
    case 0xFF40: {
        bool was_enabled = (ppu->lcdc & LCD_ENABLE) != 0;
        ppu->lcdc = value;
        if (was_enabled != ((value & LCD_ENABLE) != 0)) {
            ppu->dot = 0;
            ppu->sprite_count = 0;
            ppu->ly = 0;
            ppu->window_line = 0;
            ppu->window_y_triggered = (value & LCD_ENABLE) && ppu->wy == 0;
            ppu->frame_ready = false;
#ifdef GB_GRAPHICS_TRACE
            if ((value & LCD_ENABLE) && ppu->trace)
                graphics_trace_frame_start(ppu->trace);
#endif
            if (!(value & LCD_ENABLE)) {
                memset(ppu->framebuffer, 0, sizeof(ppu->framebuffer));
            }
        }
        break;
    }
    case 0xFF41: ppu->stat = (uint8_t)((ppu->stat & 0x07) | (value & 0x78)); break;
    case 0xFF42: ppu->scy = value; break;
    case 0xFF43: ppu->scx = value; break;
    case 0xFF44: return; // LY is read-only.
    case 0xFF45: ppu->lyc = value; break;
    case 0xFF47: ppu->bgp = value; break;
    case 0xFF48: ppu->obp0 = value; break;
    case 0xFF49: ppu->obp1 = value; break;
    case 0xFF4A: ppu->wy = value; break;
    case 0xFF4B: ppu->wx = value; break;
    default: return;
    }
    update_stat(ppu);
}
