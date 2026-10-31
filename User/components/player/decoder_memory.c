#include "decoder_memory.h"
#include <stdint.h>

static uint8_t s_memory[39U * 1024U] __attribute__((aligned(8)));
static size_t s_used;

void decoder_memory_reset(void) { s_used = 0; }

void *decoder_memory_alloc(size_t size)
{
    size_t available = sizeof(s_memory) - s_used;
    if(size > available) return NULL;
    size_t aligned = (size + 7U) & ~(size_t)7U;
    if(aligned > available) return NULL;
    void *memory = s_memory + s_used;
    s_used += aligned;
    return memory;
}
