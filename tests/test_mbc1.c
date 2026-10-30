#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static const char *rom_path = "mbc1_test_fixture.gb";
static const char *save_path = "mbc1_test_fixture.sav";

/* Each bank identifies itself at both ends; code in bank 2 also lets the CPU
 * execute across a mapper change without relying on the mapper's own state. */
static void fixture(unsigned code, unsigned type, unsigned ram)
{
    size_t size = (size_t)ROM_SIZE << code;
    uint8_t *image = malloc(size); CHECK(image);
    for (size_t bank = 0; bank < size / 0x4000; ++bank)
        memset(image + bank * 0x4000, (int)bank, 0x4000);
    image[0x147] = (uint8_t)type; image[0x148] = (uint8_t)code; image[0x149] = (uint8_t)ram;
    if (size > ROM_SIZE) { image[0x8000] = 0x3E; image[0x8001] = 0x42; }
    FILE *f = fopen(rom_path, "wb"); CHECK(f);
    CHECK(fwrite(image, 1, size, f) == size); CHECK(fclose(f) == 0); free(image);
}

static void rom_banks(void)
{
    Cartridge c; cartridge_init(&c); fixture(6, 1, 0); CHECK(load_rom(&c, rom_path));
    CHECK(c.banked_rom && !c.ram_enabled);
    CHECK(cartridge_read(&c, 0x3FFF) == 0 && cartridge_read(&c, 0x7FFF) == 1);
    /* Sweep every possible byte written to the low latch, every high bank and
     * both modes. Values above register width must be discarded. */
    for (unsigned mode = 0; mode < 2; ++mode)
        for (unsigned high = 0; high < 256; ++high)
            for (unsigned low = 0; low < 256; ++low) {
                cartridge_write(&c, 0x7FFF, (uint8_t)(0xFEu | mode));
                cartridge_write(&c, 0x5FFF, (uint8_t)high);
                cartridge_write(&c, 0x3FFF, (uint8_t)low);
                unsigned upper = (high & 3u) * 32u + ((low & 31u) ? low & 31u : 1u);
                CHECK(cartridge_read(&c, 0x7FFF) == upper);
                CHECK(cartridge_read(&c, 0x3FFF) == (mode ? (high & 3u) * 32u : 0));
            }
    /* Smaller ROMs discard disconnected address lines after zero translation. */
    for (unsigned code = 0; code <= 4; ++code) {
        fixture(code, 1, 0); CHECK(load_rom(&c, rom_path));
        CHECK((c.banked_rom != NULL) == (code != 0));
        for (unsigned value = 0; value < 32; ++value) {
            cartridge_write(&c, 0x2000, (uint8_t)value);
            unsigned expected = (value ? value : 1u) & ((2u << code) - 1u);
            CHECK(cartridge_read(&c, 0x7FFF) == expected);
        }
        cartridge_write(&c, 0x4000, 3); cartridge_write(&c, 0x6000, 1);
        CHECK(cartridge_read(&c, 0x3FFF) == 0);
    }
    cartridge_destroy(&c); cartridge_destroy(&c);
    CHECK(!c.rom_loaded && !c.banked_rom && cartridge_read(&c, 0) == 0xFF);
}

static void ram_banks(void)
{
    Cartridge c; cartridge_init(&c); fixture(4, 3, 3); CHECK(load_rom(&c, rom_path));
    CHECK(c.battery);
    cartridge_write(&c, 0xA000, 99); CHECK(cartridge_read(&c, 0xA000) == 0xFF);
    cartridge_write(&c, 0x1FFF, 0xFA); cartridge_write(&c, 0x6000, 1);
    for (unsigned bank = 0; bank < 4; ++bank) {
        cartridge_write(&c, 0x4000, (uint8_t)bank);
        cartridge_write(&c, 0xA000, (uint8_t)(0x40u + bank));
        cartridge_write(&c, 0xBFFF, (uint8_t)(0x80u + bank));
    }
    for (unsigned bank = 0; bank < 4; ++bank) {
        cartridge_write(&c, 0x4000, (uint8_t)bank);
        CHECK(cartridge_read(&c, 0xA000) == 0x40u + bank);
        CHECK(cartridge_read(&c, 0xBFFF) == 0x80u + bank);
    }
    cartridge_write(&c, 0x6000, 0); CHECK(cartridge_read(&c, 0xA000) == 0x40);
    cartridge_write(&c, 0x0000, 0); cartridge_write(&c, 0xA000, 99);
    CHECK(cartridge_read(&c, 0xA000) == 0xFF);
    cartridge_write(&c, 0x0000, 0x0A); CHECK(cartridge_read(&c, 0xA000) == 0x40);
    /* Every low-nibble enable code: upper bits do not matter. */
    for (unsigned value = 0; value < 256; ++value) {
        cartridge_write(&c, 0, (uint8_t)value);
        CHECK(cartridge_read(&c, 0xA000) == ((value & 15u) == 10 ? 0x40 : 0xFF));
    }
    fixture(5, 3, 2); CHECK(load_rom(&c, rom_path));
    cartridge_write(&c, 0, 10); cartridge_write(&c, 0xA000, 0x51);
    cartridge_write(&c, 0x4000, 1); cartridge_write(&c, 0x6000, 1);
    CHECK(cartridge_read(&c, 0x3FFF) == 32 && cartridge_read(&c, 0xA000) == 0x51);
    fixture(0, 2, 1); CHECK(load_rom(&c, rom_path)); /* Legacy 2 KiB RAM mirrors. */
    cartridge_write(&c, 0, 10); cartridge_write(&c, 0xA123, 0x67);
    CHECK(cartridge_read(&c, 0xA923) == 0x67 && cartridge_read(&c, 0xB923) == 0x67);
    CHECK(!c.battery); cartridge_destroy(&c);
}

static void persistence_and_failures(void)
{
    Cartridge c; cartridge_init(&c); fixture(2, 3, 3); CHECK(load_rom(&c, rom_path));
    /* No pre-existing file is required. RAM starts deterministically cleared. */
    CHECK(cartridge_load_save(&c, "mbc1_nonexistent_save_directory/save.sav"));
    cartridge_write(&c, 0, 10); cartridge_write(&c, 0x6000, 1);
    for (unsigned bank = 0; bank < 4; ++bank) {
        cartridge_write(&c, 0x4000, (uint8_t)bank);
        cartridge_write(&c, 0xA123, (uint8_t)(bank + 7u));
    }
    cartridge_write(&c, 0, 0); CHECK(cartridge_save(&c, save_path));
    CHECK(load_rom(&c, rom_path)); CHECK(cartridge_load_save(&c, save_path));
    cartridge_write(&c, 0, 10); cartridge_write(&c, 0x6000, 1);
    for (unsigned bank = 0; bank < 4; ++bank) {
        cartridge_write(&c, 0x4000, (uint8_t)bank);
        CHECK(cartridge_read(&c, 0xA123) == bank + 7u);
    }
    /* Failed image replacements must preserve owned storage, RAM AND latches. */
    uint8_t *original = c.banked_rom;
    fixture(6, 3, 3); CHECK(!load_rom(&c, rom_path)); /* Impossible ROM/RAM wiring. */
    fixture(7, 1, 0); CHECK(!load_rom(&c, rom_path)); /* Exceeds MBC1 ROM range. */
    fixture(0, 0x13, 2); CHECK(!load_rom(&c, rom_path)); /* MBC3 unsupported. */
    fixture(2, 1, 0);
    FILE *f = fopen(rom_path, "ab"); CHECK(f); CHECK(fputc(0, f) != EOF); CHECK(fclose(f) == 0);
    CHECK(!load_rom(&c, rom_path));
    f = fopen(rom_path, "wb"); CHECK(f); CHECK(fputc(0, f) != EOF); CHECK(fclose(f) == 0);
    CHECK(!load_rom(&c, rom_path));
    CHECK(c.banked_rom == original && c.bank_high == 3 && c.banking_mode == 1);
    CHECK(cartridge_read(&c, 0xA123) == 10);
    CHECK(!cartridge_save(&c, "mbc1_nonexistent_save_directory/save.sav"));
    /* Failed save does not damage the existing good file. */
    CHECK(cartridge_load_save(&c, save_path));
    f = fopen(save_path, "wb"); CHECK(f); CHECK(fputc(0, f) != EOF); CHECK(fclose(f) == 0);
    CHECK(!cartridge_load_save(&c, save_path)); CHECK(cartridge_read(&c, 0xA123) == 10);
    CHECK(remove(save_path) == 0);
    cartridge_destroy(&c);
}

static void multicart_rejection(void)
{
    Cartridge c; cartridge_init(&c); fixture(1, 1, 0);
    CHECK(load_rom(&c, rom_path)); uint8_t *original = c.banked_rom;
    fixture(5, 1, 0);
    static const uint8_t logo[48] = {
        0xCE,0xED,0x66,0x66,0xCC,0x0D,0,0x0B,3,0x73,0,0x83,0,0x0C,0,0x0D,
        0,8,0x11,0x1F,0x88,0x89,0,0x0E,0xDC,0xCC,0x6E,0xE6,0xDD,0xDD,0xD9,0x99,
        0xBB,0xBB,0x67,0x63,0x6E,0x0E,0xEC,0xCC,0xDD,0xDC,0x99,0x9F,0xBB,0xB9,0x33,0x3E
    };
    FILE *f = fopen(rom_path, "r+b"); CHECK(f);
    CHECK(fseek(f, 0x104, SEEK_SET) == 0); CHECK(fwrite(logo, 1, 48, f) == 48);
    CHECK(fseek(f, 0x40104, SEEK_SET) == 0); CHECK(fwrite(logo, 1, 48, f) == 48);
    CHECK(fclose(f) == 0);
    CHECK(!load_rom(&c, rom_path)); CHECK(c.banked_rom == original);
    CHECK(cartridge_read(&c, 0x7FFF) == 1);
    cartridge_destroy(&c);
}

static void cpu_and_dma(void)
{
    Memory m; CPU cpu; memory_init(&m); fixture(1, 2, 2);
    CHECK(load_rom(&m.cartridge, rom_path)); reset_cpu(&cpu, &m);
    static const uint8_t program[] = {0x3E,2,0xEA,0,0x20,0xC3,0,0x40};
    memcpy(m.wram, program, sizeof(program)); cpu.PC = 0xC000;
    CHECK(cpu_step(&cpu, &m) == 8); CHECK(cpu_step(&cpu, &m) == 16);
    CHECK(cpu_step(&cpu, &m) == 16); CHECK(cpu_step(&cpu, &m) == 8);
    CHECK(cpu.A == 0x42 && cpu.PC == 0x4002 && cpu.total_cycles == 48);
    CHECK(m.timer.divider == 48); /* Bank reads/writes added no extra time. */
    memory_write(&m, 0xFF46, 0x40); memory_tick(&m, 648);
    CHECK(m.oam[0] == 0x3E && m.oam[1] == 0x42 && m.oam[159] == 2);
    memory_write(&m, 0, 10); memory_write(&m, 0xA000, 0x76);
    memory_write(&m, 0xFF46, 0xA0); memory_tick(&m, 8);
    memory_cpu_write(&m, 0x2000, 3); memory_cpu_write(&m, 0xA000, 0);
    memory_tick(&m, 640); CHECK(m.oam[0] == 0x76);
    CHECK(memory_read(&m, 0x7FFF) == 2 && memory_read(&m, 0xA000) == 0x76);
    memory_write(&m, 0, 0); memory_write(&m, 0xFF46, 0xA0); memory_tick(&m, 648);
    CHECK(m.oam[0] == 0xFF && m.oam[159] == 0xFF);
    memory_destroy(&m); memory_destroy(&m);
}

int main(void)
{
    rom_banks(); ram_banks(); persistence_and_failures(); multicart_rejection(); cpu_and_dma();
    CHECK(remove(rom_path) == 0);
    puts("MBC1 bank, RAM, save, CPU and DMA tests passed"); return 0;
}
