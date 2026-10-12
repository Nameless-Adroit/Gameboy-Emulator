#include "cartridge.h"

#include <stdio.h>
#include <string.h>

void cartridge_init(Cartridge *cartridge)
{
    memset(cartridge, 0, sizeof(*cartridge));
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

// ------------------------------------------------------------
// Load a 32 KiB ROM-only or initially fixed-mapped MBC1 cartridge
// ------------------------------------------------------------
bool load_rom(Cartridge *cartridge, const char *filename)
{
    FILE *file = fopen(filename, "rb");
    if (file == NULL)
    {
        perror("Failed to open ROM");
        return false;
    }

    // This first implementation supports 32 KiB ROMs only.
    uint8_t buffer[ROM_SIZE];

    size_t bytes_read = fread(buffer, 1, sizeof(buffer), file);
    bool read_error = ferror(file) != 0;

    // Probe for extra bytes: larger ROMs require bank switching support.
    // Only meaningful if we filled the buffer and hit no read error.
    int extra = EOF;
    if (!read_error && bytes_read == ROM_SIZE)
    {
        extra = fgetc(file);
        read_error = ferror(file) != 0;
    }

    if (fclose(file) != 0)
    {
        perror("Failed to close ROM");
        return false;
    }

    if (read_error)
    {
        fprintf(stderr, "Failed to read ROM: %s\n", filename);
        return false;
    }

    if (bytes_read != ROM_SIZE)
    {
        fprintf(stderr,
                "ROM too small: expected %u bytes, got %zu: %s\n",
                (unsigned)ROM_SIZE, bytes_read, filename);
        return false;
    }

    if (extra != EOF)
    {
        fprintf(stderr,
                "ROM exceeds the flat 32 KiB loader (MBC not implemented): %s\n",
                filename);
        return false;
    }

    CartridgeHeader header;

    if (!cartridge_parse_header(&header, buffer, bytes_read))
    {
        fprintf(stderr, "Invalid cartridge header: %s\n", filename);
        return false;
    }

    // 00: ROM ONLY; 01: MBC1 without external RAM.
    // For this initial MBC1 implementation, map banks 0 and 1 directly
    // into 0000-3FFF and 4000-7FFF. Mapper register writes are ignored.
    bool supported_type = header.type == 0x00 || header.type == 0x01;

    if (!supported_type ||
        header.rom_size != ROM_SIZE ||
        header.ram_size != 0)
    {

        fprintf(stderr,
                "Unsupported cartridge configuration: %s\n"
                "  Type:     0x%02X\n"
                "  ROM code: 0x%02X\n"
                "  ROM size: %zu bytes\n"
                "  RAM code: 0x%02X\n"
                "  RAM size: %zu bytes\n",
                filename,
                header.type,
                header.rom_size_code,
                header.rom_size,
                header.ram_size_code,
                header.ram_size);

        return false;
    }

    memcpy(cartridge->rom, buffer, ROM_SIZE);
    cartridge->header = header;
    cartridge->rom_loaded = true;

    printf("Loaded %u bytes from %s\n", (unsigned)ROM_SIZE, filename);
    return true;
}

uint8_t cartridge_read(const Cartridge *cartridge, uint16_t address)
{
    if (cartridge->rom_loaded && address < ROM_SIZE)
    {
        return cartridge->rom[address];
    }
    return 0xFF;
}

void cartridge_write(Cartridge *cartridge, uint16_t address, uint8_t value)
{
    // Fixed mapping only: ignore ROM writes and MBC1 register writes.
    // Supported cartridges have no external RAM.
    (void)cartridge;
    (void)address;
    (void)value;
}
