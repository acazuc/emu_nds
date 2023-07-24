#include "apu.h"
#include "mem.h"

#include <stdlib.h>
#include <stdio.h>

apu_t *apu_new(mem_t *mem)
{
	apu_t *apu = calloc(sizeof(*apu), 1);
	if (!apu)
		return NULL;

	apu->mem = mem;
	return apu;
}

void apu_del(apu_t *apu)
{
	if (!apu)
		return;
	free(apu);
}

static void gen_sample(apu_t *apu, int16_t *dst)
{
	uint32_t soundcnt = mem_arm7_get_reg32(apu->mem, MEM_ARM7_REG_SOUNDCNT);
	if (!(soundcnt & (1 << 15)))
	{
		dst[0] = 0;
		dst[1] = 0;
		return;
	}
	for (size_t i = 0; i < 16; ++i)
	{
		uint32_t cnt = mem_arm7_get_reg32(apu->mem, MEM_ARM7_REG_SOUNDXCNT(i));
		if (!(cnt & (1 << 31)))
			continue;
		//printf("cnt: %08x\n", cnt);
		int16_t l;
		int16_t r;
		switch ((cnt >> 29) & 0x3)
		{
			case 0:
			{
				uint32_t sad = mem_arm7_get_reg32(apu->mem, MEM_ARM7_REG_SOUNDXSAD(i)) & 0x7FFFFFC;
				uint16_t v = mem_arm7_get16(apu->mem, sad, MEM_DIRECT);
				//printf("8bit: %08x / %x\n", sad, v);
				l = (int8_t)(uint8_t)(v >> 0) * 256;
				r = (int8_t)(uint8_t)(v >> 16) * 256;
				break;
			}
			case 1:
			{
				uint32_t sad = mem_arm7_get_reg32(apu->mem, MEM_ARM7_REG_SOUNDXSAD(i)) & 0x7FFFFFC;
				uint32_t v = mem_arm7_get32(apu->mem, sad, MEM_DIRECT);
				//printf("16bit: %08x / %x\n", sad, v);
				l = (int16_t)(uint16_t)(v >> 0);
				r = (int16_t)(uint16_t)(v >> 16);
				break;
			}
		}
		l = (l * (cnt & 0x7F)) / 127;
		r = (r * (cnt & 0x7F)) / 127;
		static const uint8_t dividers[4] = {0, 1, 2, 4};
		uint8_t divider = dividers[(cnt >> 8) & 0x3];
		l >>= divider;
		r >>= divider;
		//printf("l: %d, r: %d\n", l, r);
		dst[0] += l;
		dst[1] += r;
	}
}

void apu_cycle(apu_t *apu)
{
	if (apu->clock == apu->next_sample)
	{
		gen_sample(apu, &apu->data[apu->sample * 2]);
		apu->sample = (apu->sample + 1) % APU_FRAME_SAMPLES;
		apu->next_sample = (apu->clock + 349);
	}
	for (size_t i = 0; i < 16; ++i)
	{
		uint32_t cnt = mem_arm7_get_reg32(apu->mem, MEM_ARM7_REG_SOUNDXCNT(i));
		if (!(cnt & (1 << 31)))
			continue;
		uint16_t tmr = mem_arm7_get_reg16(apu->mem, MEM_ARM7_REG_SOUNDXTMR(i));
		while (apu->channels[i].clock >= tmr)
		{
			apu->channels[i].clock -= tmr;
			switch ((cnt >> 29) & 0x3)
			{
				case 0:
					mem_arm7_set_reg32(apu->mem, MEM_ARM7_REG_SOUNDXSAD(i),
					                   mem_arm7_get_reg32(apu->mem, MEM_ARM7_REG_SOUNDXSAD(i)) + 2);
					break;
				case 1:
					mem_arm7_set_reg32(apu->mem, MEM_ARM7_REG_SOUNDXSAD(i),
					                   mem_arm7_get_reg32(apu->mem, MEM_ARM7_REG_SOUNDXSAD(i)) + 4);
					break;
				case 2:
					break;
				case 3:
					break;
			}
		}
		apu->channels[i].clock++;
	}
	apu->clock++;
}
