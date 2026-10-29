#include "dma.h"
#include "memory.h"

void dma_start(DMA *dma, uint8_t page)
{
    // A restarted DMA keeps the old transfer running during the startup delay.
    // A later FF46 write replaces the pending request, not the committed bytes.
    dma->pending_page = page;
    dma->start_delay = 8;
}

void dma_tick(DMA *dma, Memory *memory, unsigned t_cycles)
{
    for (unsigned cycle = 0; cycle < t_cycles; ++cycle) {
        if (dma->start_delay && --dma->start_delay == 0) {
            dma->source = (uint16_t)((unsigned)dma->pending_page << 8);
            dma->index = 0;
            dma->phase = 0;
            dma->active = true;
            continue; // Byte zero completes after four active T-cycles.
        }
        if (!dma->active || ++dma->phase != 4) continue;
        dma->phase = 0;
        // FF46's documented source pages are 00..DF. Unsupported pages still
        // consume transfer time, but deterministically supply FF in this model.
        uint8_t value = dma->source < 0xE000
            ? memory_dma_read(memory, (uint16_t)(dma->source + dma->index)) : 0xFF;
        memory->oam[dma->index++] = value; // DMA master bypasses CPU restrictions.
        if (dma->index == OAM_SIZE) dma->active = false;
    }
}
