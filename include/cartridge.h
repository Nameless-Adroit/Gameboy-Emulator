#ifndef CARTRIDGE_H
#define CARTRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ROM_SIZE 0x8000 /* Inline ROM-only/two-bank fixture storage. */
#define CARTRIDGE_RAM_SIZE 0x8000

typedef struct CartridgeHeader {
    uint8_t type;
    uint8_t rom_size_code;
    uint8_t ram_size_code;
    size_t rom_size;
    size_t ram_size;
} CartridgeHeader;

typedef enum { CARTRIDGE_ROM_ONLY, CARTRIDGE_MBC1 } CartridgeMapper;

typedef struct Cartridge {
    /* Preserve direct small-ROM fixtures. Larger loaded images are owned by
     * banked_rom; do not copy an owning Cartridge/Memory by value. */
    uint8_t rom[ROM_SIZE];
    uint8_t *banked_rom;
    uint8_t ram[CARTRIDGE_RAM_SIZE];
    CartridgeHeader header;
    bool rom_loaded;
    CartridgeMapper mapper;
    bool battery;
    bool ram_enabled;
    uint8_t rom_bank_low; /* Five-bit latch; zero translates before size masking. */
    uint8_t bank_high;    /* Upper ROM bits or RAM bank, depending on wiring. */
    uint8_t banking_mode; /* 0=simple, 1=advanced. */
} Cartridge;

/* Initialize fresh storage once. Use destroy before reinitializing an owner. */
void cartridge_init(Cartridge *cartridge);
/* Frees owned ROM storage and clears state; safe to repeat after init. */
void cartridge_destroy(Cartridge *cartridge);
/* Decode header independently of mapper support. Invalid codes fail. */
bool cartridge_parse_header(CartridgeHeader *header, const uint8_t *rom, size_t size);
/* Supports ROM-only and standard MBC1 types 01/02/03. Validate exact file/header
 * sizes and wiring limits before replacing; any failure preserves old state. */
bool load_rom(Cartridge *cartridge, const char *filename);
/* Untimed mapper bus access; disabled/absent RAM reads FF and ignores writes. */
uint8_t cartridge_read(const Cartridge *cartridge, uint16_t address);
void cartridge_write(Cartridge *cartridge, uint16_t address, uint8_t value);
/* Explicit raw battery-RAM persistence. Missing input file is a fresh save;
 * malformed/read failures preserve RAM. Save uses a temporary file + rename.
 * Neither function ticks hardware or requires RAM to be bus-enabled. */
bool cartridge_load_save(Cartridge *cartridge, const char *filename);
bool cartridge_save(const Cartridge *cartridge, const char *filename);

#endif
