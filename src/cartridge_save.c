#include "cartridge.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Persistence belongs to the host, never the emulated bus. Battery-backed
 * carts use the same RAM/gating as volatile carts; only file policy differs. */
bool cartridge_load_save(Cartridge *cartridge, const char *filename)
{
    if (!cartridge->rom_loaded || !cartridge->battery || !cartridge->header.ram_size)
        return false;
    FILE *file = fopen(filename, "rb");
    if (!file) {
        if (errno == ENOENT) return true; /* First run: keep freshly initialized RAM. */
        perror("Open cartridge save");
        return false;
    }
    uint8_t buffer[CARTRIDGE_RAM_SIZE];
    size_t size = cartridge->header.ram_size;
    bool valid = fread(buffer, 1, size, file) == size;
    if (fgetc(file) != EOF || ferror(file)) valid = false;
    if (fclose(file) != 0) valid = false;
    if (!valid) { fprintf(stderr, "Invalid cartridge save size/read: %s\n", filename); return false; }
    memcpy(cartridge->ram, buffer, size);
    return true;
}

bool cartridge_save(const Cartridge *cartridge, const char *filename)
{
    if (!cartridge->rom_loaded || !cartridge->battery || !cartridge->header.ram_size)
        return false;
    size_t length = strlen(filename);
    char *temporary = malloc(length + 5u);
    if (!temporary) return false;
    memcpy(temporary, filename, length);
    memcpy(temporary + length, ".tmp", 5);
    FILE *file = fopen(temporary, "wb");
    bool valid = false;
    if (file) {
        valid = fwrite(cartridge->ram, 1, cartridge->header.ram_size, file) == cartridge->header.ram_size;
        if (fclose(file) != 0) valid = false;
        if (valid && rename(temporary, filename) != 0) valid = false;
    }
    if (!valid) {
        fprintf(stderr, "Failed to write cartridge save: %s\n", filename);
        if (file) (void)remove(temporary);
    }
    free(temporary);
    return valid;
}
