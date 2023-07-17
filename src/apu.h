#ifndef APU_H
#define APU_H

#include <stdint.h>

#define APU_FRAME_SAMPLES 803

typedef struct mem mem_t;

typedef struct apu
{
	uint16_t data[APU_FRAME_SAMPLES];
	mem_t *mem;
} apu_t;

apu_t *apu_new(mem_t *mem);
void apu_del(apu_t *apu);

void apu_cycle(apu_t *cpu);

#endif
