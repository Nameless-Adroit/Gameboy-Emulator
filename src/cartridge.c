#include "cartridge.h"

#include <stdio.h>
#include <stdlib.h>
#include "mbc1.h"
#include <string.h>

void cartridge_init(Cartridge *cartridge)
{
    memset(cartridge, 0, sizeof(*cartridge));
}

void cartridge_destroy(Cartridge *cartridge)
{
    free(cartridge->banked_rom);
    cartridge_init(cartridge);
}

bool cartridge_parse_header(CartridgeHeader *header, const uint8_t *rom,
                            size_t size)
{
    if (size < 0x150)
    {
        return false;
    }
    CartridgeHeader parsed = {0};
    parsed.type = rom[0x147];
    parsed.rom_size_code = rom[0x148];
    parsed.ram_size_code = rom[0x149];
    if (parsed.rom_size_code <= 8)
    {
        parsed.rom_size = (size_t)ROM_SIZE << parsed.rom_size_code;
    }
    else if (parsed.rom_size_code >= 0x52 && parsed.rom_size_code <= 0x54)
    {
        static const size_t banks[] = {72, 80, 96};
        parsed.rom_size = banks[parsed.rom_size_code - 0x52] * 0x4000;
    }
    else
    {
        return false;
    }
    static const size_t ram_sizes[] = {0, 0x800, 0x2000, 0x8000, 0x20000, 0x10000};
    if (parsed.ram_size_code >= sizeof(ram_sizes) / sizeof(ram_sizes[0]))
    {
        return false;
    }
    parsed.ram_size = ram_sizes[parsed.ram_size_code];
    *header = parsed;
    return true;
}


/* Reject impossible mapper/header combinations instead of silently truncating
 * ROM or allocating RAM that the declared board cannot address. */
static bool supported(const CartridgeHeader *header)
{
    if (header->type == 0)
        return header->rom_size == ROM_SIZE && header->ram_size == 0;
    if (header->type < 1 || header->type > 3 || header->rom_size_code > 6)
        return false;
    if (header->type == 1) return header->ram_size == 0;
    if (!header->ram_size || header->ram_size > CARTRIDGE_RAM_SIZE) return false;
    return header->rom_size <= 0x80000 || header->ram_size <= 0x2000;
}

bool load_rom(Cartridge *cartridge, const char *filename)
{
    FILE *file = fopen(filename, "rb");
    if (!file) { perror("Failed to open ROM"); return false; }
    uint8_t prefix[0x150];
    CartridgeHeader header;
    bool valid = fread(prefix, 1, sizeof(prefix), file) == sizeof(prefix) &&
        cartridge_parse_header(&header, prefix, sizeof(prefix));
    if (!valid || !supported(&header)) {
        fprintf(stderr, "Invalid or unsupported cartridge configuration: %s\n", filename);
        fclose(file);
        return false;
    }
    uint8_t *image = malloc(header.rom_size);
    if (!image) { fclose(file); fprintf(stderr, "Cannot allocate cartridge ROM\n"); return false; }
    memcpy(image, prefix, sizeof(prefix));
    size_t remaining = header.rom_size - sizeof(prefix);
    valid = fread(image + sizeof(prefix), 1, remaining, file) == remaining;
    /* Probe for trailing data as well as detecting truncated images. */
    if (fgetc(file) != EOF || ferror(file)) valid = false;
    if (fclose(file) != 0) valid = false;
    if (!valid) {
        fprintf(stderr, "ROM file does not match declared size (%zu bytes): %s\n",
                header.rom_size, filename);
        free(image);
        return false;
    }
    /* Standard MBC1M multicarts need different wiring. Detect the common
     * repeated-logo signature and reject instead of knowingly misbanking it. */
    static const uint8_t logo[48] = {
        0xCE,0xED,0x66,0x66,0xCC,0x0D,0,0x0B,3,0x73,0,0x83,0,0x0C,0,0x0D,
        0,8,0x11,0x1F,0x88,0x89,0,0x0E,0xDC,0xCC,0x6E,0xE6,0xDD,0xDD,0xD9,0x99,
        0xBB,0xBB,0x67,0x63,0x6E,0x0E,0xEC,0xCC,0xDD,0xDC,0x99,0x9F,0xBB,0xB9,0x33,0x3E
    };
    if (header.rom_size == 0x100000 && memcmp(image + 0x104, logo, 48) == 0 &&
        memcmp(image + 0x40104, logo, 48) == 0) {
        fprintf(stderr, "MBC1M multicart wiring is not supported: %s\n", filename);
        free(image);
        return false;
    }
    /* Everything above is transactional: no old pointers, RAM, or mapper
     * latches are changed unless the entire replacement image is valid. */
    cartridge_destroy(cartridge);
    cartridge->header = header;
    cartridge->mapper = header.type ? CARTRIDGE_MBC1 : CARTRIDGE_ROM_ONLY;
    cartridge->battery = header.type == 3;
    cartridge->rom_bank_low = 1;
    if (header.rom_size == ROM_SIZE) {
        memcpy(cartridge->rom, image, ROM_SIZE);
        free(image);
    } else cartridge->banked_rom = image;
    cartridge->rom_loaded = true;
    printf("Loaded %zu bytes from %s\n", header.rom_size, filename);
    return true;
}

uint8_t cartridge_read(const Cartridge *cartridge, uint16_t address)
{
    if (!cartridge->rom_loaded) return 0xFF;
    if (address < 0x8000) {
        const uint8_t *rom = cartridge->banked_rom ? cartridge->banked_rom : cartridge->rom;
        size_t offset = address;
        if (cartridge->mapper == CARTRIDGE_MBC1)
            offset = mbc1_rom_bank(cartridge, address) * 0x4000u + (address & 0x3FFFu);
        return rom[offset];
    }
    if (address >= 0xA000 && address <= 0xBFFF && cartridge->ram_enabled &&
        cartridge->header.ram_size)
        return cartridge->ram[mbc1_ram_offset(cartridge, address)];
    return 0xFF;
}

void cartridge_write(Cartridge *cartridge, uint16_t address, uint8_t value)
{
    if (!cartridge->rom_loaded || cartridge->mapper != CARTRIDGE_MBC1) return;
    if (address < 0x8000) mbc1_write_register(cartridge, address, value);
    else if (address >= 0xA000 && address <= 0xBFFF && cartridge->ram_enabled &&
             cartridge->header.ram_size)
        cartridge->ram[mbc1_ram_offset(cartridge, address)] = value;
}
