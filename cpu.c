#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>
#include "cpu.h"
#include "memory.h"

void reset_cpu(CPU *cpu, Memory *memory)
{
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
void alu_sub(CPU *cpu, uint8_t value);

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
        cpu->F |= flag;
    }
    else
    {
        cpu->F &= ~flag;
    }

    // Lower nibble of F must always remain zero
    cpu->F &= 0xF0;
}

bool get_flag(CPU *cpu, CPUFlag flag)
{
    return (cpu->F & flag) != 0;
}

// void set_Zflag(CPU *cpu, CPUFlag flag, bool value)
// {
//     set_flag(cpu, FLAG_Z, value == 0);
// }

// void set_Nflag(CPU *cpu, CPUFlag flag, bool value)
// {
//     set_flag(cpu, FLAG_N, value == 0);
// }

// void set_Hflag(CPU *cpu, CPUFlag flag, bool value)
// {
//     set_flag(
// cpu,
// FLAG_H,
// ((a & 0x0F) + (value & 0x0F)) > 0x0F
//);
// }

// void set_Cflag(CPU *cpu, CPUFlag flag, bool value)
// {
//     uint16_t result = cpu->A + value;

//     set_flag(cpu, FLAG_C, result > 0xFF);

//     cpu->A = (uint8_t)result;
// }

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

void cpu_step(CPU *cpu, Memory *memory)
{
    uint8_t opcode = fetch_byte(cpu, memory);

    printf(
    "PC=%04X OP=%02X "
    "A=%02X F=%02X "
    "B=%02X C=%02X "
    "H=%02X L=%02X "
    "SP=%04X\n",
    cpu->PC - 1,
    opcode,
    cpu->A,
    cpu->F,
    cpu->B,
    cpu->C,
    cpu->H,
    cpu->L,
    cpu->SP
);

    switch (opcode)
    {

    // 0x00 - NOP
    case 0x00:
        break;

    // 0x01 - LD BC,d16
    case 0x01:
    {
        uint16_t value = fetch_word(cpu, memory);
        set_reg(cpu, REG_BC, value);
        break;
    }

    // 0x02 - LD (BC),A
    case 0x02:
    {
        uint16_t address = get_reg(cpu, REG_BC);

        memory_write(
            memory,
            address,
            cpu->A);

        break;
    }

    // 0x03 - INC BC
    case 0x03:
    {
        uint16_t bc = get_reg(cpu, REG_BC);

        bc++;

        set_reg(cpu, REG_BC, bc);

        break;
    }

    // 0x04 - INC B
    case 0x04:
        cpu->B = inc8(cpu, cpu->B);
        break;

    // 0x05 - DEC B
    case 0x05:
        cpu->B = dec8(cpu, cpu->B);
        break;

    // 0x06 - LD B,d8
    case 0x06:
        cpu->B = fetch_byte(cpu, memory);
        break;

    // 0x07 - RLCA
    case 0x07:
    {

        uint8_t old_bit7 =
            (cpu->A >> 7) & 1;

        cpu->A =
            (cpu->A << 1) | old_bit7;

        set_flag(cpu, FLAG_Z, false);
        set_flag(cpu, FLAG_N, false);
        set_flag(cpu, FLAG_H, false);
        set_flag(cpu, FLAG_C, old_bit7);

        break;
    }

    // 0x08 - LD (a16),SP
    case 0x08:
    {

        uint16_t address =
            fetch_word(cpu, memory);

        memory_write(
            memory,
            address,
            (uint8_t)(cpu->SP & 0xFF));

        memory_write(
            memory,
            address + 1,
            (uint8_t)(cpu->SP >> 8));

        break;
    }

    // 0x09 - ADD HL,BC
    case 0x09:
    {

        uint16_t hl =
            get_reg(cpu, REG_HL);

        uint16_t bc =
            get_reg(cpu, REG_BC);

        uint32_t result =
            (uint32_t)hl + bc;

        set_flag(cpu, FLAG_N, false);

        set_flag(
            cpu,
            FLAG_H,
            ((hl & 0x0FFF) +
             (bc & 0x0FFF)) > 0x0FFF);

        set_flag(
            cpu,
            FLAG_C,
            result > 0xFFFF);

        set_reg(
            cpu,
            REG_HL,
            (uint16_t)result);

        break;
    }

    // 0x0A - LD A,(BC)
    case 0x0A:
    {

        uint16_t address =
            get_reg(cpu, REG_BC);

        cpu->A =
            memory_read(memory, address);

        break;
    }

    // 0x0B - DEC BC
    case 0x0B:
    {

        uint16_t bc =
            get_reg(cpu, REG_BC);

        bc--;

        set_reg(cpu, REG_BC, bc);

        break;
    }

    // 0x0C - INC C
    case 0x0C:
        cpu->C = inc8(cpu, cpu->C);
        break;

    // 0x0D - DEC C
    case 0x0D:
        cpu->C = dec8(cpu, cpu->C);
        break;

    // 0x0E - LD C,d8
    case 0x0E:
        cpu->C =
            fetch_byte(cpu, memory);
        break;

    // 0x0F - RRCA
    case 0x0F:
    {

        uint8_t old_bit0 =
            cpu->A & 1;

        cpu->A =
            (cpu->A >> 1) |
            (old_bit0 << 7);

        set_flag(cpu, FLAG_Z, false);
        set_flag(cpu, FLAG_N, false);
        set_flag(cpu, FLAG_H, false);
        set_flag(cpu, FLAG_C, old_bit0);

        break;
    }

    default:

        printf(
            "CRASH: Unimplemented Opcode "
            "0x%02X at 0x%04X\n",
            opcode,
            cpu->PC - 1);

        exit(1);
    }
}