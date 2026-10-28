#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "memory.h"
#include "cpu.h"
#include "graphics_trace.h"

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Usage: %s <rom_file.gb> [--graphics-trace] [--trace-pixel X,Y] [--trace-frames N]\n", argv[0]);
        return 1;
    }

    GraphicsTraceOptions trace_options;
    graphics_trace_options_init(&trace_options);
    for (int i = 2; i < argc; ++i)
        if (graphics_trace_option(&trace_options, argc, argv, &i) < 0) return 1;

    GraphicsTrace trace;
    CPU cpu;
    Memory memory;

    // 1. Initialize Memory and load the ROM
    memory_init(&memory);
    if (!load_rom(&memory.cartridge, argv[1]))
    {
        return 1;
    }

    // 2. Initialize CPU (Ensure PC is set to 0x0100 inside this function!)
    reset_cpu(&cpu, &memory);
    if (!graphics_trace_start(&trace, &memory, &trace_options, stderr)) return 1;

    // 3. Clock the CPU and timer, including idle cycles while HALTed.
    printf("Starting CPU Loop...\n\n");

    const unsigned long MAX_STEPS = 10000000UL;

    for (unsigned long steps = 0; steps < MAX_STEPS; steps++)
    {
        if (get_cpu_state(&cpu) == CPU_STOPPED ||
            get_cpu_state(&cpu) == CPU_LOCKED)
        {
            printf("\nCPU stopped at PC: 0x%04X\n", cpu.PC);
            break;
        }

        // CPU accesses and internal cycles already advance the hardware clock.
        cpu_step(&cpu, &memory);

        // Progress report only once per million instructions.
        if ((steps + 1) % 1000000UL == 0)
        {
            fprintf(stderr,
                    "[DEBUG] Steps=%lu PC=%04X SP=%04X A=%02X F=%02X\n",
                    steps + 1, cpu.PC, cpu.SP, cpu.A, cpu.F);
        }
    }

    printf("\n--- Blargg test status ---\n");

    uint8_t status = memory_read(&memory, 0xA000);

    uint8_t sig1 = memory_read(&memory, 0xA001);
    uint8_t sig2 = memory_read(&memory, 0xA002);
    uint8_t sig3 = memory_read(&memory, 0xA003);

    printf("Status: 0x%02X\n", status);
    printf("Signature: %02X %02X %02X\n",
           sig1, sig2, sig3);

    if (sig1 == 0xDE &&
        sig2 == 0xB0 &&
        sig3 == 0x61)
    {

        if (status == 0x00)
        {
            printf("TEST PASSED\n");
        }
        else if (status == 0x80)
        {
            printf("TEST STILL RUNNING\n");
        }
        else
        {
            printf("TEST FAILED: code %u\n", status);
        }

        printf("Test message: ");

        for (uint16_t addr = 0xA004; addr < 0xB000; addr++)
        {
            uint8_t ch = memory_read(&memory, addr);

            if (ch == 0)
            {
                break;
            }

            putchar(ch >= 32 && ch <= 126 ? ch : '.');
        }

        putchar('\n');
    }
    else
    {
        printf("No valid Blargg result signature found.\n");
    }

    printf("\n--- CPU state ---\n");
    printf("PC=%04X SP=%04X A=%02X F=%02X\n",
           cpu.PC, cpu.SP, cpu.A, cpu.F);

    printf("\n--- Memory near PC ---\n");

    for (int offset = -8; offset <= 8; offset++)
    {
        uint16_t address = (uint16_t)(cpu.PC + offset);

        printf("%04X: %02X\n",
               address,
               memory_read(&memory, address));
    }

    if (get_cpu_state(&cpu) == CPU_LOCKED)
    {
        fprintf(stderr, "CPU locked after an illegal opcode at PC: 0x%04X\n", cpu.PC);
        return 1;
    }
    printf("CPU %s at PC: 0x%04X\n",
           get_cpu_state(&cpu) == CPU_STOPPED ? "stopped" :
           get_cpu_state(&cpu) == CPU_HALTED ? "halted" : "running (step limit reached)",
           cpu.PC);
    return 0;
}
