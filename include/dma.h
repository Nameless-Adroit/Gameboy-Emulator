#ifndef DMA_H
#define DMA_H

#include <stdbool.h>
#include <stdint.h>

/* DMG OAM DMA only. This state is embedded in Memory; no allocation or second
 * clock exists. FF46 schedules a start/restart, memory_tick supplies elapsed time. */
typedef struct {
    uint16_t source;
    unsigned index;          // Completed bytes, 0..160.
    unsigned phase;          // T-cycles within the current four-cycle byte.
    unsigned start_delay;    // Pending request, two machine cycles.
    uint8_t pending_page;
    bool active;
} DMA;

struct Memory;
void dma_start(DMA *dma, uint8_t page);
void dma_tick(DMA *dma, struct Memory *memory, unsigned t_cycles);

#endif
