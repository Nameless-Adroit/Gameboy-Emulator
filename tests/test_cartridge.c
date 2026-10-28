#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "cartridge.h"

static void write_fixture(const char *path, const uint8_t *rom, size_t size,
                          bool extra)
{
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fwrite(rom, 1, size, file) == size);
    if (extra) {
        assert(fputc(0, file) != EOF);
    }
    assert(fclose(file) == 0);
}

int main(void)
{
    const char *path = "cartridge_test_fixture.gb";
    uint8_t rom[ROM_SIZE] = {0};
    Cartridge cartridge;
    CartridgeHeader header;
    cartridge_init(&cartridge);
    assert(!cartridge.rom_loaded);
    assert(cartridge_read(&cartridge, 0) == 0xFF);
    assert(!cartridge_parse_header(&header, rom, 0x14F));

    // Decode all supported size codes, independently of mapper support.
    for (uint8_t code = 0; code <= 8; ++code) {
        rom[0x148] = code;
        assert(cartridge_parse_header(&header, rom, sizeof(rom)));
        assert(header.rom_size == ((size_t)ROM_SIZE << code));
    }
    const size_t banks[] = {72, 80, 96};
    for (uint8_t code = 0x52; code <= 0x54; ++code) {
        rom[0x148] = code;
        assert(cartridge_parse_header(&header, rom, sizeof(rom)));
        assert(header.rom_size == banks[code - 0x52] * 0x4000);
    }
    rom[0x148] = 0;
    const size_t ram_sizes[] = {0, 0x800, 0x2000, 0x8000, 0x20000, 0x10000};
    for (uint8_t code = 0; code <= 5; ++code) {
        rom[0x149] = code;
        rom[0x147] = 3;
        assert(cartridge_parse_header(&header, rom, sizeof(rom)));
        assert(header.type == 3);
        assert(header.ram_size == ram_sizes[code]);
    }
    rom[0x149] = 6;
    assert(!cartridge_parse_header(&header, rom, sizeof(rom)));
    rom[0x149] = 0;
    rom[0x148] = 9;
    assert(!cartridge_parse_header(&header, rom, sizeof(rom)));

    memset(rom, 0, sizeof(rom));
    rom[0] = 0x12;
    rom[0x3FFF] = 0x34;
    rom[0x4000] = 0x56;
    rom[0x7FFF] = 0x78;
    write_fixture(path, rom, sizeof(rom), false);
    assert(load_rom(&cartridge, path));
    assert(cartridge.rom_loaded);
    assert(cartridge.header.type == 0);
    assert(cartridge.header.rom_size == ROM_SIZE);
    assert(cartridge.header.ram_size == 0);
    assert(cartridge_read(&cartridge, 0) == 0x12);
    assert(cartridge_read(&cartridge, 0x3FFF) == 0x34);
    assert(cartridge_read(&cartridge, 0x4000) == 0x56);
    assert(cartridge_read(&cartridge, 0x7FFF) == 0x78);
    cartridge_write(&cartridge, 0, 0);
    assert(cartridge_read(&cartridge, 0) == 0x12);
    cartridge_write(&cartridge, 0xA000, 0x42);
    assert(cartridge_read(&cartridge, 0xA000) == 0xFF);
    assert(cartridge_read(&cartridge, 0xBFFF) == 0xFF);
    assert(cartridge_read(&cartridge, 0x8000) == 0xFF);

    // A 32 KiB MBC1 ROM uses the same initial two-bank mapping.
    rom[0x147] = 1;
    write_fixture(path, rom, sizeof(rom), false);
    assert(load_rom(&cartridge, path));
    assert(cartridge.header.type == 1);
    assert(cartridge_read(&cartridge, 0) == 0x12);
    assert(cartridge_read(&cartridge, 0x3FFF) == 0x34);
    assert(cartridge_read(&cartridge, 0x4000) == 0x56);
    assert(cartridge_read(&cartridge, 0x7FFF) == 0x78);
    cartridge_write(&cartridge, 0x2000, 2);
    assert(cartridge_read(&cartridge, 0x4000) == 0x56);

    // RAM-bearing mapper types cannot be supported merely by declaring no RAM.
    // Reject both 02 (MBC1+RAM) and 03 (MBC1+RAM+battery), preserving state.
    for (uint8_t type = 2; type <= 3; ++type) {
        rom[0x147] = type;
        write_fixture(path, rom, sizeof(rom), false);
        assert(!load_rom(&cartridge, path));
        assert(cartridge.header.type == 1 && cartridge_read(&cartridge, 0) == 0x12);
    }

    // Failed replacement loads preserve the previously loaded ROM and header.
    rom[0x147] = 0;
    for (size_t offset = 0x147; offset <= 0x149; ++offset) {
        rom[offset] = offset == 0x147 ? 2 : 1;
        write_fixture(path, rom, sizeof(rom), false);
        assert(!load_rom(&cartridge, path));
        rom[offset] = 0;
    }
    write_fixture(path, rom, sizeof(rom) - 1, false);
    assert(!load_rom(&cartridge, path));
    write_fixture(path, rom, sizeof(rom), true);
    assert(!load_rom(&cartridge, path));
    assert(remove(path) == 0);
    assert(!load_rom(&cartridge, path));
    assert(cartridge.rom_loaded);
    assert(cartridge.header.type == 1);
    assert(cartridge.header.rom_size == ROM_SIZE);
    assert(cartridge_read(&cartridge, 0) == 0x12);
    puts("All cartridge tests passed!");
    return 0;
}
