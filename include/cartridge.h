#ifndef CARTRIDGE_H
#define CARTRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ROM_SIZE 0x8000

typedef struct CartridgeHeader {
    uint8_t type;
    uint8_t rom_size_code;
    uint8_t ram_size_code;
    size_t rom_size;
    size_t ram_size;
} CartridgeHeader;

typedef struct Cartridge {
    uint8_t rom[ROM_SIZE];
    CartridgeHeader header;
    bool rom_loaded;
} Cartridge;

void cartridge_init(Cartridge *cartridge);
// Decode header fields independently of mapper support. Invalid codes fail.
bool cartridge_parse_header(CartridgeHeader *header, const uint8_t *rom,
                            size_t size);
// Supports types 00 (ROM ONLY) and 01 (MBC1), 32 KiB, no RAM, fixed mapping.
// MBC1 bank switching is not implemented. Failure preserves the cartridge.
bool load_rom(Cartridge *cartridge, const char *filename);
uint8_t cartridge_read(const Cartridge *cartridge, uint16_t address);
void cartridge_write(Cartridge *cartridge, uint16_t address, uint8_t value);

#endif
