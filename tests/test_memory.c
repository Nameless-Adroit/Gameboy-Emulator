
#include <assert.h>
#include <stdio.h>

#include "memory.h"

int main(void)
{
    Memory memory;
    memory_init(&memory);

    // Unloaded cartridges and absent external RAM read as open bus.
    assert(memory_read(&memory, 0x0000) == 0xFF);
    assert(memory_read(&memory, 0x7FFF) == 0xFF);
    memory_write(&memory, 0xA000, 0x42);
    assert(memory_read(&memory, 0xA000) == 0xFF);
    assert(memory_read(&memory, 0xBFFF) == 0xFF);

    // 1. Work RAM read/write
    memory_write(&memory, 0xC000, 0x42);
    assert(memory_read(&memory, 0xC000) == 0x42);

    // 2. Echo RAM mirrors WRAM
    assert(memory_read(&memory, 0xE000) == 0x42);

    memory_write(&memory, 0xE123, 0xAB);
    assert(memory_read(&memory, 0xC123) == 0xAB);

    // 3. Video RAM
    memory_write(&memory, 0x8000, 0x55);
    assert(memory_read(&memory, 0x8000) == 0x55);

    // 4. High RAM
    memory_write(&memory, 0xFF80, 0x99);
    assert(memory_read(&memory, 0xFF80) == 0x99);

    // 5. Interrupt Enable register
    memory_write(&memory, 0xFFFF, 0x1F);
    assert(memory_read(&memory, 0xFFFF) == 0x1F);

    // 6. Unusable memory (our temporary model)
    memory_write(&memory, 0xFEA0, 0x77);
    assert(memory_read(&memory, 0xFEA0) == 0xFF);

    // 7. ROM-only writes are ignored
    memory.cartridge.rom[0x0100] = 0x3E;
    memory.cartridge.rom_loaded = true;

    memory_write(&memory, 0x0100, 0x00);
    assert(memory_read(&memory, 0x0100) == 0x3E);

    // Timer registers are hardware-backed, including DIV reset and TAC masks.
    assert(memory_read(&memory, 0xFF04) == 0);
    assert(memory_read(&memory, 0xFF05) == 0);
    assert(memory_read(&memory, 0xFF06) == 0);
    assert(memory_read(&memory, 0xFF07) == 0xF8);
    memory_tick(&memory, 256);
    assert(memory_read(&memory, 0xFF04) == 1);
    memory_write(&memory, 0xFF04, 0xAB);
    assert(memory_read(&memory, 0xFF04) == 0);

    // An overflow reloads TMA after four T-cycles and preserves other IF bits.
    memory_write(&memory, 0xFF0F, 0x11);
    memory_write(&memory, 0xFF06, 0x42);
    memory_write(&memory, 0xFF05, 0xFF);
    memory_write(&memory, 0xFF07, 0x05);
    assert(memory_read(&memory, 0xFF07) == 0xFD);
    memory_tick(&memory, 16);
    assert(memory_read(&memory, 0xFF05) == 0);
    assert(memory_read(&memory, 0xFF0F) == 0x11);
    memory_tick(&memory, 3);
    assert(memory_read(&memory, 0xFF05) == 0);
    memory_tick(&memory, 1);
    assert(memory_read(&memory, 0xFF05) == 0x42);
    assert(memory_read(&memory, 0xFF0F) == 0x15);

    puts("All memory bus tests passed!");
    return 0;
}
