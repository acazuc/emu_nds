#ifndef APU_H
#define APU_H

#include <stdint.h>

#define APU_FRAME_SAMPLES 803

typedef struct mem mem_t;

struct apu_channel
{
	uint16_t tmr;
	uint16_t pnt;
	uint32_t sad;
	uint32_t len;
	uint32_t pos;
	uint16_t clock;
};

typedef struct apu
{
	int16_t data[APU_FRAME_SAMPLES * 2];
	struct apu_channel channels[16];
	mem_t *mem;
	uint32_t clock;
	uint32_t sample;
	uint32_t next_sample;
} apu_t;

apu_t *apu_new(mem_t *mem);
void apu_del(apu_t *apu);

void apu_cycle(apu_t *cpu);
void apu_sample(apu_t *apu);

void apu_start_channel(apu_t *apu, uint8_t channel);

#endif
