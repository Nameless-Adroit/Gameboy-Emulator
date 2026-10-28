#ifndef PPU_H
#define PPU_H

#include <stdbool.h>
#include <stdint.h>

#define LCD_WIDTH  160
#define LCD_HEIGHT 144

typedef struct {
    // Position within the current scanline: 0..455 while enabled.
    unsigned dot;

    // Current scanline: 0..153 in this simplified timing model.
    uint8_t ly;

    // LCD hardware registers
    uint8_t lcdc;
    uint8_t stat;
    uint8_t scx;
    uint8_t scy;
    uint8_t lyc;

    uint8_t bgp;
    uint8_t obp0;
    uint8_t obp1;

    uint8_t wx;
    uint8_t wy;

    // Combined STAT source line and interrupt requests awaiting delivery.
    bool stat_irq_line;
    uint8_t pending_interrupts;

    // Borrowed 8 KiB bus VRAM; NULL leaves standalone timing tests blank.
    const uint8_t *vram;

    // BGP-mapped DMG shades: 0 white, 1 light gray, 2 dark gray, 3 black.
    uint8_t framebuffer[LCD_HEIGHT][LCD_WIDTH];

    // Latched at VBlank; the consumer clears it after handling the frame.
    // All visible background rows have been drawn before this is set.
    bool frame_ready;
} PPU;

// Deterministic reset with the LCD disabled (not post-boot register values).
void ppu_init(PPU *ppu);

// Bind bus-owned VRAM after init. Storage must outlive the PPU; no clock tick.
// Rebind after moving/copying the containing Memory; NULL detaches it.
void ppu_attach_vram(PPU *ppu, const uint8_t vram[0x2000]);

// Pure 2bpp decoding: bit 7 is the left pixel, first byte is the low plane.
// These return color IDs, not palette-mapped shades. Arrays must not overlap.
void ppu_decode_tile_row(uint8_t low, uint8_t high, uint8_t pixels[8]);
void ppu_decode_tile(const uint8_t tile[16], uint8_t pixels[8][8]);

// One dot equals one DMG T-cycle. Also delivers queued write-triggered IRQs.
// Passing zero dots delivers IRQs without advancing time.
// interrupt_flags must point to the bus IF byte. Only bits 0/1 are ORed in.
void ppu_tick(PPU *ppu,
              unsigned dots,
              uint8_t *interrupt_flags);

// Untimed register read; never mutates state. Unhandled addresses read FF.
uint8_t ppu_read(const PPU *ppu, uint16_t address);

// Register writes are untimed. Call ppu_tick(ppu, 0, IF) to deliver IRQs
// immediately after a write. FF46 (DMA) belongs to the memory bus.
void ppu_write(PPU *ppu,
               uint16_t address,
               uint8_t value);

#endif
