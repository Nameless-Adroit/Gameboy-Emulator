#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "memory.h"
#include "cpu.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <rom_file.gb>\n", argv[0]);
        return 1;
    }

    CPU cpu;
    Memory memory;

    // 1. Initialize Memory and load the ROM
    memory_init(&memory);
    if (!load_rom(&memory, argv[1])) {
        return 1;
    }

    // 2. Initialize CPU (Ensure PC is set to 0x0100 inside this function!)
    reset_cpu(&cpu, &memory);

    // 3. The Infinite Emulation Loop
    printf("Starting CPU Loop...\n\n");
    while (1) {
        cpu_step(&cpu, &memory);
        
        // Optional: Add a debug print here to watch instructions execute, 
        // but be careful, it will flood your terminal quickly!
        printf("PC: 0x%04X, A: %02X, F: %02X\n", cpu.PC, cpu.A, cpu.F);
    }

    return 0;
}