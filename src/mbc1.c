#include "mbc1.h"

size_t mbc1_rom_bank(const Cartridge *cartridge, uint16_t address)
{
    unsigned bank = 0;
    if (address >= 0x4000) {
        /* The full five-bit latch is checked BEFORE disconnected address bits
         * are masked. Thus 10 can select bank zero on a small cartridge. */
        unsigned low = cartridge->rom_bank_low ? cartridge->rom_bank_low : 1u;
        bank = ((unsigned)cartridge->bank_high << 5) | low;
    } else if (cartridge->banking_mode) {
        bank = (unsigned)cartridge->bank_high << 5;
    }
    /* Only power-of-two ROM sizes are accepted. Missing chip address lines
     * mirror banks naturally; the upper window uses high bits in BOTH modes. */
    size_t banks = cartridge->header.rom_size / 0x4000u;
    return (size_t)bank & (banks - 1u);
}

size_t mbc1_ram_offset(const Cartridge *cartridge, uint16_t address)
{
    /* Large-ROM wiring uses the secondary register for ROM, leaving only a
     * fixed RAM bank. Loader rejects large ROM + 32 KiB RAM combinations. */
    size_t bank = cartridge->banking_mode && cartridge->header.ram_size > 0x2000
        ? cartridge->bank_high : 0u;
    size_t offset = bank * 0x2000u + (size_t)(address - 0xA000);
    /* Small RAM chips omit upper address lines (including legacy 2 KiB RAM). */
    return offset & (cartridge->header.ram_size - 1u);
}

void mbc1_write_register(Cartridge *cartridge, uint16_t address, uint8_t value)
{
    if (address < 0x2000) cartridge->ram_enabled = (value & 0x0Fu) == 0x0A;
    else if (address < 0x4000) cartridge->rom_bank_low = value & 0x1Fu;
    else if (address < 0x6000) cartridge->bank_high = value & 3u;
    else cartridge->banking_mode = value & 1u;
}
