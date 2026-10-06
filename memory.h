#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>
#include <stdbool.h>

#define MEMORY_SIZE 0x10000 // 64KB of memory

typedef struct {
    uint8_t data[MEMORY_SIZE];
} Memory;

void memory_init(Memory *memory);
void memory_write(Memory *memory, uint16_t address, uint8_t value);
uint8_t memory_read(Memory *memory, uint16_t address);
bool load_rom(Memory *memory, const char *filename);

#endif