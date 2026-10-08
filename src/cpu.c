#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>
#include "cpu.h"
#include "memory.h"

void reset_cpu(CPU *cpu, Memory *memory)
{
    (void)memory; // CPU reset does not initialize the peripheral bus.
    *cpu = (CPU){0};
    // Start directly at the cartridge entry point
    cpu->PC = 0x0100;

    // Set post-boot magic numbers (what the Boot ROM leaves behind)
    cpu->A = 0x01;
    cpu->F = 0xB0;
    cpu->B = 0x00;
    cpu->C = 0x13;
    cpu->D = 0x00;
    cpu->E = 0xD8;
    cpu->H = 0x01;
    cpu->L = 0x4D;
    cpu->SP = 0xFFFE;

    cpu->state = CPU_RUNNING;
}

CPUStates get_cpu_state(CPU *cpu)
{
    return cpu->state;
}

void set_reg(CPU *cpu, Reg reg, uint16_t value)
{
    switch (reg)
    {
    case REG_A:
        cpu->A = (uint8_t)value;
        break;
    case REG_F:
        cpu->F = (uint8_t)(value & 0xF0);
        break; // Lower 4 bits always 0
    case REG_B:
        cpu->B = (uint8_t)value;
        break;
    case REG_C:
        cpu->C = (uint8_t)value;
        break;
    case REG_D:
        cpu->D = (uint8_t)value;
        break;
    case REG_E:
        cpu->E = (uint8_t)value;
        break;
    case REG_H:
        cpu->H = (uint8_t)value;
        break;
    case REG_L:
        cpu->L = (uint8_t)value;
        break;
    case REG_AF:
        cpu->A = (uint8_t)(value >> 8);
        cpu->F = (uint8_t)(value & 0xF0);
        break;
    case REG_BC:
        cpu->B = (uint8_t)(value >> 8);
        cpu->C = (uint8_t)(value & 0xFF);
        break;
    case REG_DE:
        cpu->D = (uint8_t)(value >> 8);
        cpu->E = (uint8_t)(value & 0xFF);
        break;
    case REG_HL:
        cpu->H = (uint8_t)(value >> 8);
        cpu->L = (uint8_t)(value & 0xFF);
        break;
    case REG_SP:
        cpu->SP = value;
        break;
    case REG_PC:
        cpu->PC = value;
        break;
    default:
        fprintf(stderr, "Invalid register\n");
        exit(1);
    }
}

uint16_t get_reg(CPU *cpu, Reg reg)
{
    switch (reg)
    {
    case REG_A:
        return cpu->A;
    case REG_F:
        return cpu->F;
    case REG_B:
        return cpu->B;
    case REG_C:
        return cpu->C;
    case REG_D:
        return cpu->D;
    case REG_E:
        return cpu->E;
    case REG_H:
        return cpu->H;
    case REG_L:
        return cpu->L;
    case REG_AF:
        return (uint16_t)((uint16_t)cpu->A << 8) | cpu->F;
    case REG_BC:
        return (uint16_t)((uint16_t)cpu->B << 8) | cpu->C;
    case REG_DE:
        return (uint16_t)((uint16_t)cpu->D << 8) | cpu->E;
    case REG_HL:
        return (uint16_t)((uint16_t)cpu->H << 8) | cpu->L;
    case REG_SP:
        return cpu->SP;
    case REG_PC:
        return cpu->PC;
    default:
        fprintf(stderr, "Invalid register\n");
        exit(1);
    }
}

// Fetch a single byte and advance PC
uint8_t fetch_byte(CPU *cpu, Memory *memory)
{
    uint8_t byte = memory_read(memory, cpu->PC);
    cpu->PC++;
    return byte;
}

// Fetch a 16-bit word (Little-Endian: Low byte first, then High byte)
uint16_t fetch_word(CPU *cpu, Memory *memory)
{
    uint8_t low = fetch_byte(cpu, memory);
    uint8_t high = fetch_byte(cpu, memory);
    return (uint16_t)((uint16_t)high << 8) | low;
}

// ALU Operations
void alu_add(CPU *cpu, uint8_t value)
{
    uint8_t a = cpu->A;

    uint16_t result = (uint16_t)a + value;

    cpu->A = (uint8_t)result;

    set_flag(cpu, FLAG_Z, cpu->A == 0);
    set_flag(cpu, FLAG_N, false);

    set_flag(
        cpu,
        FLAG_H,
        ((a & 0x0F) + (value & 0x0F)) > 0x0F);

    set_flag(cpu, FLAG_C, result > 0xFF);
}
void alu_sub(CPU *cpu, uint8_t value)
{
    uint8_t a = cpu->A;
    cpu->A = (uint8_t)(a - value);
    cpu->F = FLAG_N | (cpu->A == 0 ? FLAG_Z : 0)
           | ((a & 15) < (value & 15) ? FLAG_H : 0)
           | (a < value ? FLAG_C : 0);
}

// Increment an 8-bit value and set flags accordingly
uint8_t inc8(CPU *cpu, uint8_t value)
{
    uint8_t result = value + 1;

    set_flag(cpu, FLAG_Z, result == 0);
    set_flag(cpu, FLAG_N, false);
    set_flag(cpu, FLAG_H, (value & 0x0F) == 0x0F);
    return result;
}

// Decrement an 8-bit value and set flags accordingly
uint8_t dec8(CPU *cpu, uint8_t value)
{
    uint8_t result = value - 1;
    set_flag(cpu, FLAG_N, true);
    set_flag(cpu, FLAG_Z, result == 0);
    set_flag(cpu, FLAG_H, (value & 0x0F) == 0x00);
    return result;
}

// Helpers for setting and getting CPU flags
void set_flag(CPU *cpu, CPUFlag flag, bool value)
{
    if (value)
    {
        cpu->F = (uint8_t)(cpu->F | flag);
    }
    else
    {
        cpu->F = (uint8_t)(cpu->F & ~flag);
    }

    // Lower nibble of F must always remain zero
    cpu->F &= 0xF0;
}

bool get_flag(CPU *cpu, CPUFlag flag)
{
    return (cpu->F & flag) != 0;
}

// Stack Operations
void push_word(CPU *cpu, Memory *memory, uint16_t value)
{
    uint8_t high = (uint8_t)(value >> 8);
    uint8_t low  = (uint8_t)(value & 0xFF);

    cpu->SP--;
    memory_write(memory, cpu->SP, high);

    cpu->SP--;
    memory_write(memory, cpu->SP, low);
}

uint16_t pop_word(CPU *cpu, Memory *memory)
{
    uint8_t low = memory_read(memory, cpu->SP);
    cpu->SP++;

    uint8_t high = memory_read(memory, cpu->SP);
    cpu->SP++;

    return (uint16_t)((high << 8) | low);
}

/* The SM83 encodes operands in bit fields. r8 order is B,C,D,E,H,L,(HL),A;
 * pair order is BC,DE,HL,SP (AF replaces SP in PUSH/POP). Keep this mapping
 * separate from the public Reg enum, whose numeric order is different. */
static const Reg pairs[4] = { REG_BC, REG_DE, REG_HL, REG_SP };
static const Reg stack_pairs[4] = { REG_BC, REG_DE, REG_HL, REG_AF };
static const Reg registers[8] = {
    REG_B, REG_C, REG_D, REG_E, REG_H, REG_L, REG_HL, REG_A
};

static uint8_t read_r8(CPU *cpu, Memory *memory, unsigned r)
{
    return r == 6 ? memory_read(memory, get_reg(cpu, REG_HL))
                  : (uint8_t)get_reg(cpu, registers[r]);
}

static void write_r8(CPU *cpu, Memory *memory, unsigned r, uint8_t value)
{
    if (r == 6) memory_write(memory, get_reg(cpu, REG_HL), value);
    else set_reg(cpu, registers[r], value);
}

// Conditions appear in the order NZ, Z, NC, C in branch opcodes.
static bool condition(CPU *cpu, unsigned cc)
{
    bool flag = get_flag(cpu, cc < 2 ? FLAG_Z : FLAG_C);
    return (cc & 1) ? flag : !flag;
}

/* ADD, ADC, SUB, SBC, AND, XOR, OR, CP share this operand decoder.
 * Widen before adding the carry: FF + carry must not wrap to zero first.
 * CP has the same flags as SUB but leaves A untouched. */
static void alu(CPU *cpu, unsigned operation, uint8_t value)
{
    unsigned a = cpu->A;
    unsigned carry = ((operation == 1 || operation == 3) && get_flag(cpu, FLAG_C));
    unsigned result;
    switch (operation) {
    case 0: case 1:
        result = a + value + carry;
        cpu->A = (uint8_t)result;
        cpu->F = (cpu->A == 0 ? FLAG_Z : 0)
               | (((a & 15) + (value & 15) + carry > 15) ? FLAG_H : 0)
               | (result > 255 ? FLAG_C : 0);
        break;
    case 2: case 3: case 7:
        result = (uint8_t)(a - value - carry);
        cpu->F = FLAG_N | (result == 0 ? FLAG_Z : 0)
               | ((a & 15) < ((value & 15) + carry) ? FLAG_H : 0)
               | (a < (unsigned)value + carry ? FLAG_C : 0);
        if (operation != 7) cpu->A = (uint8_t)result;
        break;
    case 4: cpu->A &= value; cpu->F = FLAG_H; break;
    case 5: cpu->A ^= value; cpu->F = 0; break;
    case 6: cpu->A |= value; cpu->F = 0; break;
    }
    if (operation >= 4 && operation <= 6)
        set_flag(cpu, FLAG_Z, cpu->A == 0);
}

// RLC, RRC, RL, RR, SLA, SRA, SWAP, SRL (CB field y = 0..7).
static uint8_t rotate_shift(CPU *cpu, unsigned operation, uint8_t value)
{
    unsigned carry = 0;
    unsigned old_carry = get_flag(cpu, FLAG_C);
    uint8_t result = value;
    switch (operation) {
    case 0: carry = value >> 7; result = (uint8_t)((value << 1) | carry); break;
    case 1: carry = value & 1; result = (uint8_t)((value >> 1) | (carry << 7)); break;
    case 2: carry = value >> 7; result = (uint8_t)((value << 1) | old_carry); break;
    case 3: carry = value & 1; result = (uint8_t)((value >> 1) | (old_carry << 7)); break;
    case 4: carry = value >> 7; result = (uint8_t)(value << 1); break;
    case 5: carry = value & 1; result = (uint8_t)((value >> 1) | (value & 0x80)); break;
    case 6: result = (uint8_t)((value << 4) | (value >> 4)); break;
    case 7: carry = value & 1; result = value >> 1; break;
    }
    cpu->F = (result == 0 ? FLAG_Z : 0) | (carry ? FLAG_C : 0);
    return result;
}

static unsigned execute_cb(CPU *cpu, Memory *memory)
{
    uint8_t op = fetch_byte(cpu, memory);
    unsigned group = op >> 6, bit = (op >> 3) & 7, r = op & 7;
    uint8_t value = read_r8(cpu, memory, r);
    if (group == 0) {
        write_r8(cpu, memory, r, rotate_shift(cpu, bit, value));
    } else if (group == 1) {
        // BIT only reads its operand; carry survives, N clears, H sets.
        cpu->F = (cpu->F & FLAG_C) | FLAG_H
               | ((value & (1u << bit)) == 0 ? FLAG_Z : 0);
    } else {
        // RES and SET preserve every flag, including Z.
        value = group == 2 ? (uint8_t)(value & ~(1u << bit))
                           : (uint8_t)(value | (1u << bit));
        write_r8(cpu, memory, r, value);
    }
    // These totals include the prefix fetch. BIT (HL) has no write cycle.
    return r == 6 ? (group == 1 ? 12 : 16) : 8;
}

static void add_hl(CPU *cpu, uint16_t value)
{
    unsigned hl = get_reg(cpu, REG_HL), result = hl + value;
    cpu->F = (cpu->F & FLAG_Z)
           | (((hl & 0xFFF) + (value & 0xFFF) > 0xFFF) ? FLAG_H : 0)
           | (result > 0xFFFF ? FLAG_C : 0);
    set_reg(cpu, REG_HL, (uint16_t)result);
}

// Avoid implementation-defined unsigned-to-signed narrowing in C99.
static int signed_byte(uint8_t value)
{
    return value < 0x80 ? value : (int)value - 0x100;
}

static uint16_t add_sp_offset(CPU *cpu, uint8_t offset)
{
    unsigned sp = cpu->SP;
    // H/C use the unsigned immediate's low nibble/byte, even for negatives.
    cpu->F = (((sp & 15) + (offset & 15) > 15) ? FLAG_H : 0)
           | (((sp & 255) + offset > 255) ? FLAG_C : 0);
    return (uint16_t)((int)sp + signed_byte(offset));
}

static void daa(CPU *cpu)
{
    unsigned correction = 0;
    bool carry = get_flag(cpu, FLAG_C);
    if (!get_flag(cpu, FLAG_N)) {
        if (carry || cpu->A > 0x99) { correction |= 0x60; carry = true; }
        if (get_flag(cpu, FLAG_H) || (cpu->A & 15) > 9) correction |= 0x06;
        cpu->A = (uint8_t)(cpu->A + correction);
    } else {
        // Subtraction trusts the prior H/C flags; no magnitude tests here.
        if (carry) correction |= 0x60;
        if (get_flag(cpu, FLAG_H)) correction |= 0x06;
        cpu->A = (uint8_t)(cpu->A - correction);
    }
    cpu->F = (cpu->F & FLAG_N) | (cpu->A == 0 ? FLAG_Z : 0)
           | (carry ? FLAG_C : 0);
}

static unsigned execute(CPU *cpu, Memory *memory, uint8_t op)
{
    unsigned r = (op >> 3) & 7, pair = (op >> 4) & 3;
    uint16_t address, value;
    uint8_t byte;

    if (op >= 0x40 && op <= 0x7F) {
        if (op == 0x76) {
            // HALT bug: pending enabled interrupt with IME=0 inhibits the
            // next opcode fetch's PCa increment instead of entering HALT.
            if (!cpu->ime && (memory_read(memory, 0xFFFF)
                           & memory_read(memory, 0xFF0F) & 0x1F))
                cpu->halt_bug = true;
            else cpu->state = CPU_HALTED;
            return 4;
        }
        write_r8(cpu, memory, r, read_r8(cpu, memory, op & 7));
        return r == 6 || (op & 7) == 6 ? 8 : 4;
    }
    if (op >= 0x80 && op <= 0xBF) {
        alu(cpu, r, read_r8(cpu, memory, op & 7));
        return (op & 7) == 6 ? 8 : 4;
    }
    if (op < 0x40) {
        if ((op & 7) == 4) {
            byte = inc8(cpu, read_r8(cpu, memory, r));
            write_r8(cpu, memory, r, byte);
            return r == 6 ? 12 : 4;
        }
        if ((op & 7) == 5) {
            byte = dec8(cpu, read_r8(cpu, memory, r));
            write_r8(cpu, memory, r, byte);
            return r == 6 ? 12 : 4;
        }
        if ((op & 7) == 6) {
            byte = fetch_byte(cpu, memory);
            write_r8(cpu, memory, r, byte);
            return r == 6 ? 12 : 8;
        }
        if ((op & 15) == 1) {
            value = fetch_word(cpu, memory);
            set_reg(cpu, pairs[pair], value);
            return 12;
        }
        if ((op & 15) == 3 || (op & 15) == 11) {
            value = get_reg(cpu, pairs[pair]);
            value = (uint16_t)(value + ((op & 8) ? -1 : 1));
            set_reg(cpu, pairs[pair], value); // 16-bit INC/DEC preserve flags.
            return 8;
        }
        if ((op & 15) == 9) {
            add_hl(cpu, get_reg(cpu, pairs[pair]));
            return 8;
        }
        if ((op & 7) == 2) {
            // BC, DE, HL+, HL- indirect loads, in both directions.
            address = get_reg(cpu, pair < 2 ? pairs[pair] : REG_HL);
            if (op & 8) cpu->A = memory_read(memory, address);
            else memory_write(memory, address, cpu->A);
            if (pair >= 2)
                set_reg(cpu, REG_HL, (uint16_t)(address + (pair == 2 ? 1 : -1)));
            return 8;
        }
    }
    if (op >= 0xC0) {
        if ((op & 0xE7) == 0xC0) { // RET cc
            if (!condition(cpu, (op >> 3) & 3)) return 8;
            cpu->PC = pop_word(cpu, memory);
            return 20;
        }
        if ((op & 0xE7) == 0xC2) { // JP cc,a16
            address = fetch_word(cpu, memory);
            if (!condition(cpu, (op >> 3) & 3)) return 12;
            cpu->PC = address;
            return 16;
        }
        if ((op & 0xE7) == 0xC4) { // CALL cc,a16
            address = fetch_word(cpu, memory);
            if (!condition(cpu, (op >> 3) & 3)) return 12;
            push_word(cpu, memory, cpu->PC);
            cpu->PC = address;
            return 24;
        }
        if ((op & 0xCF) == 0xC1) {
            set_reg(cpu, stack_pairs[pair], pop_word(cpu, memory));
            return 12; // set_reg masks the unused F bits for POP AF.
        }
        if ((op & 0xCF) == 0xC5) {
            push_word(cpu, memory, get_reg(cpu, stack_pairs[pair]));
            return 16;
        }
        if ((op & 7) == 6) {
            alu(cpu, r, fetch_byte(cpu, memory));
            return 8;
        }
        if ((op & 7) == 7) { // RST vectors: 00,08,10,18,20,28,30,38.
            push_word(cpu, memory, cpu->PC);
            cpu->PC = (uint16_t)(op & 0x38);
            return 16;
        }
    }
    switch (op) {
    case 0x00: return 4; // NOP
    case 0x07: case 0x0F: case 0x17: case 0x1F:
        cpu->A = rotate_shift(cpu, op >> 3, cpu->A);
        cpu->F &= (uint8_t)~FLAG_Z; // Unprefixed accumulator rotates clear Z.
        return 4;
    case 0x08:
        address = fetch_word(cpu, memory);
        memory_write(memory, address, (uint8_t)cpu->SP);
        memory_write(memory, (uint16_t)(address + 1), (uint8_t)(cpu->SP >> 8));
        return 20;
    case 0x10:
        (void)fetch_byte(cpu, memory); // Conventional two-byte STOP 00.
        cpu->state = CPU_STOPPED;
        return 4; // CGB speed switching and hardware STOP quirks are bus work.
    case 0x18:
        byte = fetch_byte(cpu, memory);
        cpu->PC = (uint16_t)(cpu->PC + signed_byte(byte));
        return 12;
    case 0x20: case 0x28: case 0x30: case 0x38:
        byte = fetch_byte(cpu, memory); // Operand is consumed even if not taken.
        if (!condition(cpu, (op >> 3) & 3)) return 8;
        cpu->PC = (uint16_t)(cpu->PC + signed_byte(byte));
        return 12;
    case 0x27: daa(cpu); return 4;
    case 0x2F: cpu->A = (uint8_t)~cpu->A; cpu->F |= FLAG_N | FLAG_H; return 4;
    case 0x37: cpu->F = (cpu->F & FLAG_Z) | FLAG_C; return 4;
    case 0x3F: cpu->F = (cpu->F & FLAG_Z) | ((cpu->F ^ FLAG_C) & FLAG_C); return 4;
    case 0xC3: cpu->PC = fetch_word(cpu, memory); return 16;
    case 0xC9: cpu->PC = pop_word(cpu, memory); return 16;
    case 0xCB: return execute_cb(cpu, memory);
    case 0xCD:
        address = fetch_word(cpu, memory);
        push_word(cpu, memory, cpu->PC);
        cpu->PC = address;
        return 24;
    case 0xD9:
        cpu->PC = pop_word(cpu, memory);
        cpu->ime = true; cpu->ime_delay = 0;
        return 16;
    case 0xE0:
        address = (uint16_t)(0xFF00u + fetch_byte(cpu, memory));
        memory_write(memory, address, cpu->A); return 12;
    case 0xE2: memory_write(memory, (uint16_t)(0xFF00u + cpu->C), cpu->A); return 8;
    case 0xE8:
        byte = fetch_byte(cpu, memory);
        cpu->SP = add_sp_offset(cpu, byte); return 16;
    case 0xE9: cpu->PC = get_reg(cpu, REG_HL); return 4; // JP HL, no memory read.
    case 0xEA:
        address = fetch_word(cpu, memory);
        memory_write(memory, address, cpu->A); return 16;
    case 0xF0:
        address = (uint16_t)(0xFF00u + fetch_byte(cpu, memory));
        cpu->A = memory_read(memory, address); return 12;
    case 0xF2: cpu->A = memory_read(memory, (uint16_t)(0xFF00u + cpu->C)); return 8;
    case 0xF3: cpu->ime = false; cpu->ime_delay = 0; return 4;
    case 0xF8:
        byte = fetch_byte(cpu, memory);
        set_reg(cpu, REG_HL, add_sp_offset(cpu, byte)); return 12;
    case 0xF9: cpu->SP = get_reg(cpu, REG_HL); return 8;
    case 0xFA:
        address = fetch_word(cpu, memory);
        cpu->A = memory_read(memory, address); return 16;
    case 0xFB:
        // A second EI must not postpone the first EI's pending enable.
        if (!cpu->ime && !cpu->ime_delay) cpu->ime_delay = 2;
        return 4;
    default:
        // D3 DB DD E3 E4 EB EC ED F4 FC FD lock the actual CPU.
        cpu->state = CPU_LOCKED;
        return 4;
    }
}

void cpu_wake_stop(CPU *cpu)
{
    if (cpu->state == CPU_STOPPED) cpu->state = CPU_RUNNING;
}

unsigned cpu_step(CPU *cpu, Memory *memory)
{
    unsigned cycles;
    uint8_t pending;
    cpu->cycles = 0;
    if (cpu->state == CPU_STOPPED) return 0;
    if (cpu->state == CPU_LOCKED) {
        cycles = 4;
        goto finish;
    }
    pending = memory_read(memory, 0xFFFF) & memory_read(memory, 0xFF0F) & 0x1F;
    if (pending) {
        cpu->state = CPU_RUNNING; // HALT wakes even when IME is disabled.
        if (cpu->ime) {
            unsigned interrupt = 0;
            while (!(pending & (1u << interrupt))) ++interrupt;
            cpu->ime = false;
            cpu->ime_delay = 0;
            // EI; HALT with an already pending interrupt returns to HALT.
            if (cpu->halt_bug) --cpu->PC;
            cpu->halt_bug = false;
            memory_write(memory, 0xFF0F,
                         (uint8_t)(memory_read(memory, 0xFF0F) & ~(1u << interrupt)));
            push_word(cpu, memory, cpu->PC);
            cpu->PC = (uint16_t)(0x40 + interrupt * 8);
            cycles = 20;
            goto finish;
        }
    }
    if (cpu->state == CPU_HALTED) {
        cycles = 4; // Peripheral clocks continue while instruction fetching stops.
    } else {
        uint8_t opcode = memory_read(memory, cpu->PC);
        if (cpu->halt_bug) cpu->halt_bug = false;
        else ++cpu->PC;
        cycles = execute(cpu, memory, opcode);
        if (cpu->ime_delay && --cpu->ime_delay == 0) cpu->ime = true;
    }
finish:
    cpu->F &= 0xF0;
    cpu->cycles = cycles;
    cpu->total_cycles += cycles;
    return cycles;
}
