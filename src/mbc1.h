#ifndef MBC1_H
#define MBC1_H
#include "cartridge.h"
/* Private standard-MBC1 bank selection, separate from files and bus dispatch. */
size_t mbc1_rom_bank(const Cartridge *cartridge, uint16_t address);
size_t mbc1_ram_offset(const Cartridge *cartridge, uint16_t address);
void mbc1_write_register(Cartridge *cartridge, uint16_t address, uint8_t value);
#endif
