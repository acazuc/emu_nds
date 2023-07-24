#ifndef APU_H
#define APU_H

#include <stdint.h>

#define APU_FRAME_SAMPLES 803

typedef struct mem mem_t;

typedef struct apu
{
	int16_t data[APU_FRAME_SAMPLES * 2];
	mem_t *mem;
	struct
	{
		uint16_t clock;
	} channels[16];
	uint32_t clock;
	uint32_t sample;
	uint32_t next_sample;
} apu_t;

apu_t *apu_new(mem_t *mem);
void apu_del(apu_t *apu);

void apu_cycle(apu_t *cpu);

#endif
