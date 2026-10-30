#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include "memory.h"
#include "cpu.h"
#include "graphics_trace.h"
#include "mbc1.h"

static bool diagnostic_number(const char *text, uint64_t *value)
{
    char *end;
    errno = 0;
    unsigned long long n = strtoull(text, &end, 10);
    if (errno || text[0] < '0' || text[0] > '9' || *end) return false;
    *value = (uint64_t)n;
    return true;
}

static uint32_t framebuffer_hash(const Memory *m)
{
    uint32_t hash = 2166136261u;
    for (unsigned y = 0; y < LCD_HEIGHT; ++y)
        for (unsigned x = 0; x < LCD_WIDTH; ++x)
            hash = (hash ^ m->ppu.framebuffer[y][x]) * 16777619u;
    return hash;
}

/* Untimed snapshots before cpu_step: opcode is the byte currently visible on
 * the CPU bus, not a claim that HALT or interrupt entry executes that byte. */
static void cpu_snapshot(const CPU *cpu, Memory *m, uint64_t step)
{
    static const char *states[] = {"RUNNING", "HALTED", "STOPPED", "LOCKED"};
    size_t bank = m->cartridge.mapper == CARTRIDGE_MBC1
        ? mbc1_rom_bank(&m->cartridge, 0x4000) : 1;
    fprintf(stderr, "[cpu] step=%" PRIu64 " cycle=%" PRIu64
            " PC=%04X SP=%04X opcode=%02X state=%s IME=%u IE=%02X IF=%02X"
            " LY=%u STAT=%02X LCDC=%02X bank=%zu A=%02X F=%02X"
            " DIV=%04X TIMA=%02X TAC=%02X SB=%02X SC=%02X fb=%08" PRIX32 "\n",
            step, cpu->total_cycles, cpu->PC, cpu->SP,
            memory_cpu_read(m, cpu->PC), states[cpu->state],
            cpu->ime ? 1u : 0u, m->ie, memory_read(m, 0xFF0F),
            m->ppu.ly, ppu_read(&m->ppu, 0xFF41), m->ppu.lcdc, bank,
            cpu->A, cpu->F, m->timer.divider, m->timer.tima, m->timer.tac,
            memory_read(m, 0xFF01), memory_read(m, 0xFF02),
            framebuffer_hash(m));
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Usage: %s <rom_file.gb> [--graphics-trace] [--trace-pixel X,Y] [--trace-frames N]\n"
               "  [--steps N] [--cpu-trace] [--cpu-trace-start N]\n"
               "  [--cpu-trace-count 1..4096] [--cpu-trace-every N]\n", argv[0]);
        return 1;
    }

    GraphicsTraceOptions trace_options;
    bool cpu_trace = false;
    uint64_t max_steps = 10000000, trace_start = 0, trace_count = 256, trace_every = 1;
    graphics_trace_options_init(&trace_options);
    for (int i = 2; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "--cpu-trace") == 0 || strcmp(arg, "--trace") == 0) {
            cpu_trace = true; continue;
        }
        uint64_t *value = NULL;
        if (strcmp(arg, "--steps") == 0) value = &max_steps;
        else if (strcmp(arg, "--cpu-trace-start") == 0) value = &trace_start;
        else if (strcmp(arg, "--cpu-trace-count") == 0) value = &trace_count;
        else if (strcmp(arg, "--cpu-trace-every") == 0) value = &trace_every;
        if (value) {
            if (++i >= argc || !diagnostic_number(argv[i], value) ||
                (value != &trace_start && *value == 0) ||
                (value == &trace_count && *value > 4096)) {
                fprintf(stderr, "Invalid %s\n", arg); return 1;
            }
            if (value != &max_steps) cpu_trace = true;
            continue;
        }
        int parsed = graphics_trace_option(&trace_options, argc, argv, &i);
        if (parsed <= 0) {
            if (!parsed) fprintf(stderr, "Unknown option: %s\n", arg);
            return 1;
        }
    }

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
    if (!graphics_trace_start(&trace, &memory, &trace_options, stderr)) {
        memory_destroy(&memory);
        return 1;
    }

    // 3. Clock the CPU and timer, including idle cycles while HALTed.
    printf("Starting CPU Loop...\n\n");

    uint64_t snapshots = 0, frames = 0, changed_frames = 0, steps;
    uint32_t last_frame = framebuffer_hash(&memory);
    for (steps = 0; steps < max_steps; steps++)
    {
        if (cpu_trace && snapshots < trace_count && steps >= trace_start &&
            (steps - trace_start) % trace_every == 0) {
            cpu_snapshot(&cpu, &memory, steps);
            ++snapshots;
        }
        if (get_cpu_state(&cpu) == CPU_STOPPED ||
            get_cpu_state(&cpu) == CPU_LOCKED)
        {
            printf("\nCPU stopped at PC: 0x%04X\n", cpu.PC);
            break;
        }

        // CPU accesses and internal cycles already advance the hardware clock.
        cpu_step(&cpu, &memory);
        if (cpu_trace && memory.ppu.frame_ready) {
            uint32_t hash = framebuffer_hash(&memory);
            ++frames;
            if (hash != last_frame) ++changed_frames;
            last_frame = hash;
            memory.ppu.frame_ready = false;
        }

        // Progress report only once per million instructions.
        if ((steps + 1) % 1000000UL == 0)
        {
            fprintf(stderr,
                    "[DEBUG] Steps=%" PRIu64 " PC=%04X SP=%04X A=%02X F=%02X\n",
                    steps + 1, cpu.PC, cpu.SP, cpu.A, cpu.F);
        }
    }
    if (cpu_trace) {
        cpu_snapshot(&cpu, &memory, steps);
        fprintf(stderr, "[cpu-summary] samples=%" PRIu64 " frames=%" PRIu64
                " changed_frames=%" PRIu64 " final_fb=%08" PRIX32 "\n",
                snapshots, frames, changed_frames, framebuffer_hash(&memory));
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
        memory_destroy(&memory);
        return 1;
    }
    printf("CPU %s at PC: 0x%04X\n",
           get_cpu_state(&cpu) == CPU_STOPPED ? "stopped" :
           get_cpu_state(&cpu) == CPU_HALTED ? "halted" : "running (step limit reached)",
           cpu.PC);
    memory_destroy(&memory);
    return 0;
}
