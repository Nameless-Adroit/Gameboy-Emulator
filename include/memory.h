#ifndef MEMORY_H
#define MEMORY_H

#include "cartridge.h"
#include "timer.h"
#include "serial.h"
#include "apu.h"
#include "joypad.h"
#include "dma.h"
#include "ppu.h"
#include <stdint.h>

// Original Game Boy (DMG) memory regions
#define VRAM_SIZE 0x2000
#define WRAM_SIZE 0x2000
#define OAM_SIZE 0x00A0
#define IO_SIZE 0x0080
#define HRAM_SIZE 0x007F

typedef struct Memory
{
    Cartridge cartridge;

    // Memory inside the Game Boy
    uint8_t vram[VRAM_SIZE];
    uint8_t wram[WRAM_SIZE];
    uint8_t oam[OAM_SIZE];
    uint8_t io[IO_SIZE];
    uint8_t hram[HRAM_SIZE];

    // Interrupt Enable register, address FFFF
    uint8_t ie;

    Joypad joypad;
    Timer timer;
    Serial serial;
    APU apu;
    DMA dma;
    PPU ppu;
#ifdef GB_GRAPHICS_TRACE
    struct GraphicsTrace *trace; // Borrowed optional observer; never owns time.
#endif

} Memory;

// Initialization
/* Initialize fresh storage. Destroy an owning instance before reinitializing. */
void memory_init(Memory *memory);
/* Release cartridge-owned storage; call after the frontend releases borrowed state. */
void memory_destroy(Memory *memory);

// Raw memory bus interface: reads and writes never advance time.
uint8_t memory_read(Memory *memory, uint16_t address);
void memory_write(Memory *memory,
                  uint16_t address,
                  uint8_t value);

// CPU-facing bus policy, still untimed. Timed CPU helpers clock first, then
// sample these functions; DMA/PPU/debuggers use the raw interface above.
uint8_t memory_cpu_read(Memory *memory, uint16_t address);
void memory_cpu_write(Memory *memory, uint16_t address, uint8_t value);

// DMA master reads the raw map without claiming CPU/graphics-trace provenance.
uint8_t memory_dma_read(Memory *memory, uint16_t address);

// Advance peripherals by elapsed CPU T-cycles.
void memory_tick(Memory *memory, unsigned t_cycles);

#endif
