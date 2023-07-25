#include "apu.h"
#include "mem.h"

#include <inttypes.h>
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
		struct apu_channel *channel = &apu->channels[i];
		int16_t l;
		int16_t r;
		switch ((cnt >> 29) & 0x3)
		{
			case 0:
			{
				uint16_t v = mem_arm7_get16(apu->mem,
				                            channel->sad + channel->pos,
				                            MEM_DIRECT);
#if 0
				printf("8bit: %08x / %x\n", channel->sad + channel->pos, v);
#endif
				l = (int8_t)(uint8_t)(v >> 0) * 256;
				r = (int8_t)(uint8_t)(v >> 16) * 256;
				break;
			}
			case 1:
			{
				uint32_t v = mem_arm7_get32(apu->mem,
				                            channel->sad + channel->pos,
				                            MEM_DIRECT);
#if 0
				printf("16bit: %08x / %x\n", channel->sad + channel->pos, v);
#endif
				l = (int16_t)(uint16_t)(v >> 0);
				r = (int16_t)(uint16_t)(v >> 16);
				break;
			}
			default:
				continue;
		}
		uint8_t volume = cnt & 0x7F;
		l = (l * volume) / 128;
		r = (r * volume) / 128;
		static const uint8_t dividers[4] = {0, 1, 2, 4};
		uint8_t divider = dividers[(cnt >> 8) & 0x3];
		l >>= divider;
		r >>= divider;
		uint8_t pan = (cnt >> 16) & 0x7F;
		l = (l * 127 - pan) / 128;
		r = (r * pan) / 128;
		dst[0] += l;
		dst[1] += r;
	}
	uint8_t volume = soundcnt & 0x7F;
	dst[0] = (dst[0] * volume) / 128;;
	dst[1] = (dst[1] * volume) / 128;;
}

void apu_sample(apu_t *apu)
{
	if (apu->clock == apu->next_sample)
	{
#if 0
		printf("sample %u\n", apu->sample);
#endif
		gen_sample(apu, &apu->data[apu->sample * 2]);
		apu->sample = (apu->sample + 1) % APU_FRAME_SAMPLES;
		apu->next_sample = (apu->clock + 1395);
	}
	apu->clock++;
}

void apu_cycle(apu_t *apu)
{
	for (size_t i = 0; i < 16; ++i)
	{
		uint32_t cnt = mem_arm7_get_reg32(apu->mem, MEM_ARM7_REG_SOUNDXCNT(i));
		if (!(cnt & (1 << 31)))
			continue;
		struct apu_channel *channel = &apu->channels[i];
		switch ((cnt >> 29) & 0x3)
		{
			case 0:
				channel->pos += 2;
				break;
			case 1:
				channel->pos += 4;
				break;
			case 2:
				channel->pos += 1;
				break;
			case 3:
				break;
		}
		if (channel->pos >= channel->len + channel->pnt)
		{
			switch ((cnt >> 27) & 0x3)
			{
				case 0:
				case 2:
				case 3:
					mem_arm7_set_reg32(apu->mem, MEM_ARM7_REG_SOUNDXCNT(i), 
					                   mem_arm7_get_reg32(apu->mem, MEM_ARM7_REG_SOUNDXCNT(i)) & ~(1 << 31));
					break;
				case 1:
					channel->pos = channel->pnt;
					break;
			}
		}
	}
}

void apu_start_channel(apu_t *apu, uint8_t id)
{
	struct apu_channel *channel = &apu->channels[id];
	channel->pnt = (mem_arm7_get_reg16(apu->mem, MEM_ARM7_REG_SOUNDXPNT(id)) & 0x3FFFFF) * 4;
	channel->tmr = mem_arm7_get_reg16(apu->mem, MEM_ARM7_REG_SOUNDXTMR(id));
	channel->sad = mem_arm7_get_reg32(apu->mem, MEM_ARM7_REG_SOUNDXSAD(id));
	channel->len = (mem_arm7_get_reg32(apu->mem, MEM_ARM7_REG_SOUNDXLEN(id)) & 0x3FFFFF) * 4;
	channel->pos = 0;
#if 0
	printf("APU start %u: CNT=%08" PRIx32 " SAD=%08" PRIx32 " TMR=%04" PRIx16 " PNT=%04" PRIx16 " LEN=%08" PRIx32 "\n",
	       id, mem_arm7_get_reg32(apu->mem, MEM_ARM7_REG_SOUNDXCNT(id)),
	       channel->sad, channel->tmr, channel->pnt, channel->len);
#endif
}
