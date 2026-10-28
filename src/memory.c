#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "memory.h"

// ------------------------------------------------------------
// Initialize emulator memory
// ------------------------------------------------------------

void memory_init(Memory *memory)
{
    memset(memory, 0, sizeof(*memory));
    cartridge_init(&memory->cartridge);
    ppu_init(&memory->ppu);
    ppu_attach_vram(&memory->ppu, memory->vram);
}

// ------------------------------------------------------------
// Read one byte from the CPU address space
// ------------------------------------------------------------

uint8_t memory_read(Memory *memory, uint16_t address)
{
    // 0000-7FFF: Cartridge ROM
    if (address <= 0x7FFF) {
        return cartridge_read(&memory->cartridge, address);
    }

    // 8000-9FFF: Video RAM
    if (address <= 0x9FFF) {
        return memory->vram[address - 0x8000];
    }

    // A000-BFFF: External cartridge RAM
    if (address <= 0xBFFF) {
        return cartridge_read(&memory->cartridge, address);
    }

    // C000-DFFF: Work RAM
    if (address <= 0xDFFF) {
        return memory->wram[address - 0xC000];
    }

    // E000-FDFF: Echo of C000-DDFF
    if (address <= 0xFDFF) {
        return memory->wram[address - 0xE000];
    }

    // FE00-FE9F: Sprite Attribute Memory
    if (address <= 0xFE9F) {
        return memory->oam[address - 0xFE00];
    }

    // FEA0-FEFF: Unusable memory
    if (address <= 0xFEFF) {
        return 0xFF;  // Temporary simplification
    }

    // FF04-FF07: Timer registers have hardware-specific behavior.
    if (address >= 0xFF04 && address <= 0xFF07) {
        return timer_read(&memory->timer, address);
    }

    // LCD registers; FF46 is reserved for the future DMA controller.
    if (address >= 0xFF40 && address <= 0xFF4B && address != 0xFF46) {
        return ppu_read(&memory->ppu, address);
    }

    // FF00-FF7F: Hardware I/O registers
    if (address <= 0xFF7F) {
        return memory->io[address - 0xFF00];
    }

    // FF80-FFFE: High RAM
    if (address <= 0xFFFE) {
        return memory->hram[address - 0xFF80];
    }

    // FFFF: Interrupt Enable register
    return memory->ie;
}

// ------------------------------------------------------------
// Write one byte to the CPU address space
// ------------------------------------------------------------

void memory_write(Memory *memory,
                  uint16_t address,
                  uint8_t value)
{
    // 0000-7FFF: ROM-only cartridge
    // Writes do not alter ROM contents.
    if (address <= 0x7FFF) {
        cartridge_write(&memory->cartridge, address, value);
        return;
    }

    // 8000-9FFF: Video RAM
    if (address <= 0x9FFF) {
        memory->vram[address - 0x8000] = value;
        return;
    }

    // A000-BFFF: External cartridge RAM
    if (address <= 0xBFFF) {
        cartridge_write(&memory->cartridge, address, value);
        return;
    }

    // C000-DFFF: Work RAM
    if (address <= 0xDFFF) {
        memory->wram[address - 0xC000] = value;
        return;
    }

    // E000-FDFF: Echo of Work RAM
    if (address <= 0xFDFF) {
        memory->wram[address - 0xE000] = value;
        return;
    }

    // FE00-FE9F: Sprite Attribute Memory
    if (address <= 0xFE9F) {
        memory->oam[address - 0xFE00] = value;
        return;
    }

    // FEA0-FEFF: Unusable region
    if (address <= 0xFEFF) {
        return;
    }

    // FF04-FF07: Route writes through the timer's register logic.
    if (address >= 0xFF04 && address <= 0xFF07) {
        timer_write(&memory->timer, address, value);
        return;
    }

    // Writes may request STAT immediately but never advance the clock.
    if (address >= 0xFF40 && address <= 0xFF4B && address != 0xFF46) {
        ppu_write(&memory->ppu, address, value);
        ppu_tick(&memory->ppu, 0, &memory->io[0x0F]);
        return;
    }

    // FF00-FF7F: Hardware I/O registers
    if (address <= 0xFF7F) {
        memory->io[address - 0xFF00] = value;

        // Temporary serial test-ROM output.
        // Real serial timing will be implemented later.
        if (address == 0xFF02 && value == 0x81) {
            putchar(memory->io[0x01]);
            fflush(stdout);
        }

        return;
    }

    // FF80-FFFE: High RAM
    if (address <= 0xFFFE) {
        memory->hram[address - 0xFF80] = value;
        return;
    }

    // FFFF: Interrupt Enable
    memory->ie = value;
}

void memory_tick(Memory *memory, unsigned t_cycles)
{
    // FF0F is IF; the timer requests its interrupt by setting bit 2.
    timer_tick(&memory->timer, &memory->io[0x0F], t_cycles);
    // DMG: one PPU dot per CPU T-cycle, including HALT idle time.
    ppu_tick(&memory->ppu, t_cycles, &memory->io[0x0F]);
}
