#ifndef CPU_H
#define CPU_H

#include <stdint.h>
#include<stdbool.h>
#include "memory.h"

typedef enum {
    CPU_RUNNING,
    CPU_HALTED,
    CPU_STOPPED
} CPUStates;

typedef struct {
    uint8_t A, F, B, C, D, E, H, L;
    uint16_t SP;
    uint16_t PC;
    CPUStates state;
} CPU;

typedef enum {
    REG_A, REG_F, REG_B, REG_C, REG_D, REG_E, REG_H, REG_L,
    REG_AF, REG_BC, REG_DE, REG_HL, REG_SP, REG_PC
} Reg;

typedef enum {
    FLAG_Z = 0x80,
    FLAG_N = 0x40,
    FLAG_H = 0x20,
    FLAG_C = 0x10
} CPUFlag;

void      reset_cpu(CPU *cpu, Memory *memory);
CPUStates get_cpu_state(CPU *cpu);

void      set_reg(CPU *cpu, Reg reg, uint16_t value);
uint16_t  get_reg(CPU *cpu, Reg reg);

uint8_t   fetch_byte(CPU *cpu, Memory *memory);
uint16_t  fetch_word(CPU *cpu, Memory *memory);

void alu_add(CPU *cpu, uint8_t value);
void alu_sub(CPU *cpu, uint8_t value);
uint8_t inc8(CPU *cpu, uint8_t value);
uint8_t dec8(CPU *cpu, uint8_t value);

void set_flag(CPU *cpu, CPUFlag flag, bool value);
bool get_flag(CPU *cpu, CPUFlag flag);

void push_word(CPU *cpu, Memory *memory, uint16_t value);
uint16_t pop_word(CPU *cpu, Memory *memory);
void      cpu_step(CPU *cpu, Memory *memory);

#endif