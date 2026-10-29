#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "memory.h"
#include "graphics_trace.h"

// ------------------------------------------------------------
// Initialize emulator memory
// ------------------------------------------------------------

void memory_init(Memory *memory)
{
    memset(memory, 0, sizeof(*memory));
    cartridge_init(&memory->cartridge);
    joypad_init(&memory->joypad);
    apu_init(&memory->apu);
    ppu_init(&memory->ppu);
    ppu_attach_vram(&memory->ppu, memory->vram);
    ppu_attach_oam(&memory->ppu, memory->oam, &memory->dma.active);
}

// ------------------------------------------------------------
// Read one byte from the CPU address space
// ------------------------------------------------------------

static uint8_t read_bus(Memory *memory, uint16_t address, bool trace_rom)
{
#ifndef GB_GRAPHICS_TRACE
    (void)trace_rom;
#endif
    // 0000-7FFF: Cartridge ROM
    if (address <= 0x7FFF) {
        uint8_t value = cartridge_read(&memory->cartridge, address);
#ifdef GB_GRAPHICS_TRACE
        if (trace_rom && memory->trace) graphics_trace_rom(memory->trace, address, value);
#endif
        return value;
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

    if (address == 0xFF00) return joypad_read(&memory->joypad);
    if (address >= 0xFF10 && address <= 0xFF3F) return apu_read(&memory->apu, address);

    // FF04-FF07: Timer registers have hardware-specific behavior.
    if (address >= 0xFF04 && address <= 0xFF07) {
        return timer_read(&memory->timer, address);
    }

    // LCD registers; FF46 is owned by the DMA controller.
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

uint8_t memory_read(Memory *memory, uint16_t address)
{
    return read_bus(memory, address, true);
}

uint8_t memory_dma_read(Memory *memory, uint16_t address)
{
    return read_bus(memory, address, false);
}

/* Only instruction bus transactions pass here. Interrupt-controller sampling
 * of IF/IE and DMA master transfers must not be mistaken for CPU data reads. */
static bool cpu_access_blocked(const Memory *memory, uint16_t address)
{
    if (memory->dma.active && !(address >= 0xFF80 && address <= 0xFFFE))
        return true;
    if (memory->ppu.lcdc & 0x80) {
        unsigned mode = memory->ppu.stat & 3u;
        if (address >= 0x8000 && address <= 0x9FFF && mode == 3) return true;
        if (address >= 0xFE00 && address <= 0xFE9F && (mode == 2 || mode == 3))
            return true;
    }
    return false;
}

uint8_t memory_cpu_read(Memory *memory, uint16_t address)
{
    return cpu_access_blocked(memory, address) ? 0xFF : memory_read(memory, address);
}

void memory_cpu_write(Memory *memory, uint16_t address, uint8_t value)
{
    // This exception permits a running HRAM routine to restart DMA. FF46 reads
    // remain blocked during DMA, just like the other non-HRAM CPU reads.
    if (address == 0xFF46 || !cpu_access_blocked(memory, address))
        memory_write(memory, address, value);
}

// ------------------------------------------------------------
// Write one byte to the CPU address space
// ------------------------------------------------------------

void memory_write(Memory *memory,
                  uint16_t address,
                  uint8_t value)
{
#ifdef GB_GRAPHICS_TRACE
    if (memory->trace)
        graphics_trace_write(memory->trace, memory->vram, memory->ppu.lcdc,
            memory->ppu.scx, memory->ppu.scy, address, value);
#endif
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

    if (address == 0xFF00) {
        joypad_write(&memory->joypad, value, &memory->io[0x0F]);
        return;
    }

    if (address >= 0xFF10 && address <= 0xFF3F) {
        apu_write(&memory->apu, address, value);
        return;
    }

    // FF04-FF07: Route writes through the timer's register logic.
    if (address >= 0xFF04 && address <= 0xFF07) {
        if (address == 0xFF04) apu_div_reset(&memory->apu, memory->timer.divider);
        timer_write(&memory->timer, address, value);
        return;
    }

    if (address == 0xFF46) {
        memory->io[0x46] = value; // Raw FF46 reads expose the last written page.
        dma_start(&memory->dma, value);
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

static void tick_devices(Memory *memory, unsigned t_cycles)
{
    // APU observes the same divider interval as TIMA, including wraparound.
    apu_tick(&memory->apu, t_cycles, memory->timer.divider);
    timer_tick(&memory->timer, &memory->io[0x0F], t_cycles);
    ppu_tick(&memory->ppu, t_cycles, &memory->io[0x0F]);
    if (memory->dma.active || memory->dma.start_delay)
        dma_tick(&memory->dma, memory, t_cycles);
#ifdef GB_GRAPHICS_TRACE
    if (memory->trace) memory->trace->cycles += t_cycles;
#endif
}

void memory_tick(Memory *memory, unsigned t_cycles)
{
    // Interleave devices only while needed. When DMA ends inside a large
    // batch, the remainder returns immediately to ordinary batched ticking.
    while (t_cycles && (memory->dma.active || memory->dma.start_delay)) {
        tick_devices(memory, 1);
        --t_cycles;
    }
    tick_devices(memory, t_cycles); // Also preserves zero-cycle IRQ flushing.
}
