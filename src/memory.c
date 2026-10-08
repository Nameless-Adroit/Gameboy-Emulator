#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include "memory.h"

void memory_init(Memory *memory) {
    memset(memory->data, 0, MEMORY_SIZE);
}

void memory_write(Memory *memory, uint16_t address, uint8_t value) {
    if (address < MEMORY_SIZE) {
        memory->data[address] = value;
        
        // INTERCEPT SERIAL OUTPUT for Test ROMs
        if (address == 0xFF02 && value == 0x81) {
            char c = memory->data[0xFF01];
            printf("%c", c);
            fflush(stdout); // Force it to print immediately
        }
    } else {
        fprintf(stderr, "Memory write error: Address 0x%"PRIx16" is out of bounds\n", address);
    }
}

uint8_t memory_read(Memory *memory, uint16_t address) {
    if (address < MEMORY_SIZE) {
        return memory->data[address];
    } else {
        fprintf(stderr, "Memory read error: Address 0x%"PRIx16" is out of bounds\n", address);
        return 0; // Return 0 for out-of-bounds reads
    }
}

bool load_rom(Memory *memory, const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "Failed to open ROM file: %s\n", filename);
        return false;
    }

    size_t bytes_read = fread(memory->data, 1, 0x8000, file);

    if (bytes_read == 0 || ferror(file)) {
        fprintf(stderr, "Failed to read ROM: %s\n", filename);
        fclose(file);
        return false;
    }

     // Reject banked ROMs instead of silently pretending the first 32 KiB is all
    // of the cartridge. MBC support needs a separate cartridge/bus abstraction.
    int extra = fgetc(file);
    if (extra != EOF || ferror(file)) {
        fprintf(stderr, "ROM exceeds the flat 32 KiB loader (MBC not implemented): %s\n", filename);
        fclose(file);
        return false;
    }

    printf("Loaded %zu bytes from %s\n", bytes_read, filename);
    fclose(file);
    return true;
}