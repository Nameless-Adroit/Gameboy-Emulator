#include "ppu.h"

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

/* Render a whole row at the end of mode 3. VRAM and LCD registers are sampled
 * here, not per pixel: raster effects within a scanline are an approximation.
 * Reads bypass the CPU bus and never advance time. */
static void render_background_line(PPU *ppu)
{
    uint8_t *output = ppu->framebuffer[ppu->ly];
    if (!(ppu->lcdc & 0x01) || ppu->vram == NULL) {
        // DMG BG disabled is white regardless of BGP's color-zero mapping.
        memset(output, 0, LCD_WIDTH);
        return;
    }
    unsigned map = (ppu->lcdc & 0x08) ? 0x1C00u : 0x1800u;
    unsigned y = ((unsigned)ppu->scy + ppu->ly) & 0xFFu;
    uint8_t row[8];
    for (unsigned x = 0; x < LCD_WIDTH; ++x) {
        unsigned bg_x = ((unsigned)ppu->scx + x) & 0xFFu;
        if (x == 0 || (bg_x & 7u) == 0) {
            uint8_t id = ppu->vram[map + (y / 8) * 32 + bg_x / 8];
            unsigned tile_offset;
            if (ppu->lcdc & 0x10) {
                tile_offset = (unsigned)id * 16; // Unsigned, base 8000.
            } else {
                int signed_id = id < 128 ? (int)id : (int)id - 256;
                tile_offset = (unsigned)(0x1000 + signed_id * 16); // Base 9000.
            }
            unsigned offset = tile_offset + (y & 7u) * 2;
            ppu_decode_tile_row(ppu->vram[offset], ppu->vram[offset + 1], row);
        }
        unsigned color = row[bg_x & 7u];
        output[x] = (uint8_t)(((unsigned)ppu->bgp >> (color * 2)) & 3u);
    }
}

void ppu_tick(PPU *ppu, unsigned dots, uint8_t *interrupt_flags)
{
    if (ppu->lcdc & LCD_ENABLE) {
        for (unsigned i = 0; i < dots; ++i) {
            ++ppu->dot;
            if (ppu->ly < LCD_HEIGHT &&
                ppu->dot == OAM_SCAN_DOTS + TRANSFER_DOTS)
                render_background_line(ppu);
            if (ppu->dot == DOTS_PER_LINE) {
                ppu->dot = 0;
                if (++ppu->ly == LINES_PER_FRAME) ppu->ly = 0;
                if (ppu->ly == LCD_HEIGHT) {
                    ppu->frame_ready = true;
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
            ppu->ly = 0;
            ppu->frame_ready = false;
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
