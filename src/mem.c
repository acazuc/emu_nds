#include "mem.h"
#include "cpu.h"
#include "nds.h"
#include "mbc.h"
#include "apu.h"

#include <inttypes.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <time.h>

static const uint16_t timer_masks[4] = {0, 0x7E, 0x1FE, 0x7FE};

static const uint32_t dma_len_max[4] = {0x4000, 0x4000, 0x4000, 0x10000};

static const uint8_t arm7_mram_cycles_32[] = {0, 2, 10, 2,  9};
static const uint8_t arm7_mram_cycles_16[] = {0, 1,  9, 1,  8};
static const uint8_t arm7_mram_cycles_8[]  = {0, 1,  9, 1,  8};

static const uint8_t arm7_wram_cycles_32[] = {0, 1,  1, 1,  1};
static const uint8_t arm7_wram_cycles_16[] = {0, 1,  1, 1,  1};
static const uint8_t arm7_wram_cycles_8[]  = {0, 1,  1, 1,  1};

static const uint8_t arm7_vram_cycles_32[] = {0, 2,  1, 2,  2};
static const uint8_t arm7_vram_cycles_16[] = {0, 1,  1, 1,  1};
static const uint8_t arm7_vram_cycles_8[]  = {0, 1,  1, 1,  1};

static const uint8_t arm9_mram_cycles_32[] = {0, 4, 20, 18, 18};
static const uint8_t arm9_mram_cycles_16[] = {0, 2, 18,  9,  9};
static const uint8_t arm9_mram_cycles_8[]  = {0, 2, 18,  9,  9};

static const uint8_t arm9_wram_cycles_32[] = {0, 2,  8,  8,  8};
static const uint8_t arm9_wram_cycles_16[] = {0, 2,  8,  4,  4};
static const uint8_t arm9_wram_cycles_8[]  = {0, 2,  8,  4,  4};

static const uint8_t arm9_vram_cycles_32[] = {0, 4, 10, 10, 10};
static const uint8_t arm9_vram_cycles_16[] = {0, 2,  8,  5,  5};
static const uint8_t arm9_vram_cycles_8[]  = {0, 2,  8,  5,  5};

static const uint8_t arm9_tcm_cycles_32[]  = {0, 1,  1, 1,  1};
static const uint8_t arm9_tcm_cycles_16[]  = {0, 1,  1, 1,  1};
static const uint8_t arm9_tcm_cycles_8[]   = {0, 1,  1, 1,  1};

mem_t *mem_new(nds_t *nds, mbc_t *mbc)
{
	mem_t *mem = calloc(sizeof(*mem), 1);
	if (!mem)
		return NULL;

	mem->nds = nds;
	mem->mbc = mbc;
	mem->arm7_wram_base = 0;
	mem->arm7_wram_mask = 0;
	mem->arm9_wram_base = 0;
	mem->arm9_wram_base = 0x7FFF;
	mem->arm9_regs[MEM_ARM7_REG_ROMCTRL + 2] = 0x80;
	mem_arm7_set_reg32(mem, MEM_ARM7_REG_SOUNDBIAS, 0x200);
	mem_arm7_set_reg32(mem, MEM_ARM7_REG_POWCNT2, 1);
	mem->vram_bga_base = MEM_VRAM_A_BASE;
	mem->vram_bga_mask = MEM_VRAM_A_MASK;
	mem->vram_bgb_base = MEM_VRAM_C_BASE;
	mem->vram_bgb_mask = MEM_VRAM_C_MASK;
	mem->vram_obja_base = MEM_VRAM_B_BASE;
	mem->vram_obja_mask = MEM_VRAM_B_MASK;
	mem->vram_objb_base = MEM_VRAM_D_BASE;
	mem->vram_objb_mask = MEM_VRAM_D_MASK;
	mem->spi_powerman.regs[0x0] = 0x0C; /* enable backlight */
	mem->spi_powerman.regs[0x4] = 0x42; /* high brightness */
	return mem;
}

void mem_del(mem_t *mem)
{
	if (!mem)
		return;
	free(mem);
}

#define ARM_TIMERS(armv) \
static void arm##armv##_timers(mem_t *mem) \
{ \
	bool prev_overflowed = false; \
	for (unsigned i = 0; i < 4; ++i) \
	{ \
		uint8_t cnt_h = mem_arm##armv##_get_reg8(mem, MEM_ARM##armv##_REG_TM0CNT_H + i * 4); \
		if (!(cnt_h & (1 << 7))) \
			goto next_timer; \
		if (i && (cnt_h & (1 << 2))) \
		{ \
			if (!prev_overflowed) \
				goto next_timer; \
		} \
		else \
		{ \
			if ((mem->nds->cycle & timer_masks[cnt_h & 3])) \
				goto next_timer; \
		} \
		mem->arm##armv##_timers[i].v++; \
		if (!mem->arm##armv##_timers[i].v) \
		{ \
			/* printf("[ARM" #armv "] timer %u overflow (cnt_h: %02" PRIx8 ")\n", i, cnt_h); */ \
			mem->arm##armv##_timers[i].v = mem_arm##armv##_get_reg16(mem, MEM_ARM##armv##_REG_TM0CNT_L + i * 4); \
			if (cnt_h & (1 << 6)) \
				mem_arm##armv##_if(mem, 1 << (3 + i)); \
			prev_overflowed = true; \
			continue; \
		} \
next_timer: \
		prev_overflowed = false; \
	} \
} \
static void arm##armv##_timer_control(mem_t *mem, uint8_t timer, uint8_t v) \
{ \
	uint8_t prev = mem_arm##armv##_get_reg8(mem, MEM_ARM##armv##_REG_TM0CNT_H + timer * 4); \
	mem_arm##armv##_set_reg8(mem, MEM_ARM##armv##_REG_TM0CNT_H + timer * 4, v); \
	if ((v & (1 << 7)) && !(prev & (1 << 7))) \
		mem->arm##armv##_timers[timer].v = mem_arm##armv##_get_reg16(mem, MEM_ARM##armv##_REG_TM0CNT_L + timer * 4); \
}

ARM_TIMERS(7);
ARM_TIMERS(9);

void mem_timers(mem_t *mem)
{
	arm7_timers(mem);
	arm9_timers(mem);
}

#define ARM_DMA(armv) \
static void arm##armv##_dma(mem_t *mem) \
{ \
	for (size_t i = 0; i < 4; ++i) \
	{ \
		struct dma *dma = &mem->arm##armv##_dma[i]; \
		if (dma->status != (MEM_DMA_ACTIVE | MEM_DMA_ENABLE) ) \
			continue; \
		uint16_t cnt_h = mem_arm##armv##_get_reg16(mem, MEM_ARM##armv##_REG_DMA0CNT_H + 0xC * i); \
		uint32_t step; \
		if (cnt_h & (1 << 10)) \
		{ \
			/* printf("DMA 32 from 0x%" PRIx32 " to 0x%" PRIx32 "\n", dma->src, dma->dst); */ \
			mem_arm##armv##_set32(mem, dma->dst, \
			                      mem_arm##armv##_get32(mem, dma->src, MEM_DIRECT), \
			                      MEM_DIRECT); \
			step = 4; \
		} \
		else \
		{ \
			/* printf("DMA 16 from 0x%" PRIx32 " to 0x%" PRIx32 "\n", dma->src, dma->dst); */ \
			mem_arm##armv##_set16(mem, dma->dst, \
			                      mem_arm##armv##_get16(mem, dma->src, MEM_DIRECT), \
			                      MEM_DIRECT); \
			step = 2; \
		} \
		switch ((cnt_h >> 5) & 3) \
		{ \
			case 0: \
				dma->dst += step; \
				break; \
			case 1: \
				dma->dst -= step; \
				break; \
			case 2: \
				break; \
			case 3: \
				dma->dst += step; \
				break; \
		} \
		switch ((cnt_h >> 7) & 3) \
		{ \
			case 0: \
				dma->src += step; \
				break; \
			case 1: \
				dma->src -= step; \
				break; \
			case 2: \
				break; \
			case 3: \
				break; \
		} \
		dma->cnt++; \
		if (dma->cnt == dma->len) \
		{ \
			if ((cnt_h & (1 << 9))) \
			{ \
				if (((cnt_h >> 12) & 0x3) != 0x2 \
				 || !(mem->arm9_regs[MEM_ARM9_REG_ROMCTRL + 3] & (1 << 7))) \
					dma->status &= ~MEM_DMA_ACTIVE; \
			} \
			else \
			{ \
				dma->status = 0; \
			} \
			dma->cnt = 0; \
			if (!(dma->status & MEM_DMA_ACTIVE)) \
				mem_arm##armv##_set_reg16(mem, MEM_ARM##armv##_REG_DMA0CNT_H + 0xC * i, \
				                          mem_arm##armv##_get_reg16(mem, MEM_ARM##armv##_REG_DMA0CNT_H + 0xC * i) & ~(1 << 15)); \
			if (cnt_h & (1 << 14)) \
				mem_arm##armv##_if(mem, (1 << (8 + i))); \
		} \
		return; \
	} \
} \
static void arm##armv##_load_dma_length(mem_t *mem, size_t id) \
{ \
	struct dma *dma = &mem->arm##armv##_dma[id]; \
	dma->len = mem_arm##armv##_get_reg16(mem, MEM_ARM##armv##_REG_DMA0CNT_L + 0xC * id); \
	if (armv == 7) \
	{ \
		if (dma->len) \
		{ \
			if (dma->len > dma_len_max[id]) \
				dma->len = dma_len_max[id]; \
		} \
		else \
		{ \
			dma->len = dma_len_max[id]; \
		} \
	} \
	else \
	{ \
		if (dma->len) \
		{ \
			dma->len &= 0x1FFFFF; \
		} \
		else \
		{ \
			dma->len = 0x200000; \
		} \
	} \
} \
static void arm##armv##_dma_control(mem_t *mem, uint8_t id) \
{ \
	struct dma *dma = &mem->arm##armv##_dma[id]; \
	dma->src = mem_arm##armv##_get_reg32(mem, MEM_ARM##armv##_REG_DMA0SAD + 0xC * id); \
	dma->dst = mem_arm##armv##_get_reg32(mem, MEM_ARM##armv##_REG_DMA0DAD + 0xC * id); \
	dma->cnt = 0; \
	arm##armv##_load_dma_length(mem, id); \
	uint16_t cnt_h = mem_arm##armv##_get_reg16(mem, MEM_ARM##armv##_REG_DMA0CNT_H + 0xC * id); \
	dma->status = 0; \
	if (cnt_h & (1 << 15)) \
		dma->status |= MEM_DMA_ENABLE; \
	if (armv == 7) \
	{ \
		if (!(cnt_h & (3 << 12))) \
			dma->status |= MEM_DMA_ACTIVE; \
	} \
	else \
	{ \
		if (!(cnt_h & (7 << 11))) \
			dma->status |= MEM_DMA_ACTIVE; \
	} \
	if (0 && (dma->status & MEM_DMA_ENABLE)) \
		printf("enable DMA %" PRIu8 " of %08" PRIx32 " words from %08" PRIx32 " to %08" PRIx32 ": %04" PRIx16 "\n", \
		       id, dma->len, dma->src, dma->dst, cnt_h); \
} \
static void arm##armv##_dma_start(mem_t *mem, uint8_t cond) \
{ \
	for (unsigned i = 0; i < 4; ++i) \
	{ \
		struct dma *dma = &mem->arm##armv##_dma[i]; \
		if (!(dma->status & MEM_DMA_ENABLE) \
		 || (dma->status & MEM_DMA_ACTIVE)) \
			continue; \
		uint16_t cnt_h = mem_arm##armv##_get_reg16(mem, MEM_ARM##armv##_REG_DMA0CNT_H + 0xC * i); \
		if (armv == 7) \
		{ \
			if (((cnt_h >> 12) & 0x3) != cond) \
				continue; \
		} \
		else \
		{ \
			if (((cnt_h >> 11) & 0x7) != cond) \
				continue; \
		} \
		if (((cnt_h >> 5) & 0x3) == 0x3) \
			dma->dst = mem_arm##armv##_get_reg32(mem, MEM_ARM##armv##_REG_DMA0DAD + 0xC * i); \
		arm##armv##_load_dma_length(mem, i); \
		dma->cnt = 0; \
		dma->status |= MEM_DMA_ACTIVE; \
		/* printf("start DMA %u of %08" PRIx32 " words from %08" PRIx32 " to %08" PRIx32 "\n", i, dma->len, dma->src, dma->dst); */ \
	} \
}

ARM_DMA(7);
ARM_DMA(9);

void mem_dma(mem_t *mem)
{
	arm7_dma(mem);
	arm9_dma(mem);
}

void mem_vblank(mem_t *mem)
{
	arm7_dma_start(mem, 1);
	arm9_dma_start(mem, 1);
}

void mem_hblank(mem_t *mem)
{
	arm9_dma_start(mem, 2);
}

void mem_dscard(mem_t *mem)
{
	arm7_dma_start(mem, 2);
	arm9_dma_start(mem, 5);
}

static uint8_t powerman_read(mem_t *mem)
{
#if 0
	printf("[%08" PRIx32 "] SPI powerman read 0x%02" PRIx8 "\n",
	       cpu_get_reg(mem->nds->arm7, CPU_REG_PC),
	       mem->spi_powerman.read_latch);
#endif
	return mem->spi_powerman.read_latch;
}

static uint8_t firmware_read(mem_t *mem)
{
#if 0
	printf("[%08" PRIx32 "] SPI firmware read 0x%02" PRIx8 "\n",
	       cpu_get_reg(mem->nds->arm7, CPU_REG_PC),
	       mem->spi_firmware.read_latch);
#endif
	return mem->spi_firmware.read_latch;
}

static uint8_t touchscreen_read(mem_t *mem)
{
	uint8_t v;
	if (!mem->spi_touchscreen.read_pos)
	{
		v = mem->spi_touchscreen.read_latch >> 5;
		mem->spi_touchscreen.read_pos = 1;
	}
	else
	{
		v = mem->spi_touchscreen.read_latch << 3;
	}
#if 0
	printf("[%08" PRIx32 "] SPI touchscreen read 0x%02" PRIx8 "\n",
	       cpu_get_reg(mem->nds->arm7, CPU_REG_PC), v);
#endif
	return v;
}

static void powerman_write(mem_t *mem, uint8_t v)
{
#if 0
	printf("[%08" PRIx32 "] SPI powerman write 0x%02" PRIx8 "\n",
	       cpu_get_reg(mem->nds->arm7, CPU_REG_PC), v);
#endif
	if (mem->spi_powerman.has_cmd)
	{
		static const uint8_t regs[8] = {0, 1, 2, 3, 4, 4, 4, 4};
		uint8_t reg = regs[mem->spi_powerman.cmd & 0x7];
		if (mem->spi_powerman.cmd & (1 << 7))
		{
			uint8_t val = mem->spi_powerman.regs[reg];
#if 0
			printf("SPI powerman read reg[0x%" PRIx8 "] = 0x%02" PRIx8 "\n",
			       reg, val);
#endif
			mem->spi_powerman.read_latch = val;
		}
		else
		{
			static const uint8_t write_masks[5] = {0x7F, 0x00, 0x01, 0x03, 0x07};
			mem->spi_powerman.regs[reg] = (v & write_masks[reg])
			                            | (mem->spi_powerman.regs[reg] & ~write_masks[reg]);
#if 0
			printf("SPI powerman write reg[0x%" PRIx8 "] = 0x%02" PRIx8 "\n",
			       reg, v);
#endif
		}
		return;
	}
	if (mem->arm7_regs[MEM_ARM7_REG_SPICNT + 1] & (1 << 3))
	{
		mem->spi_powerman.has_cmd = 1;
		mem->spi_powerman.cmd = v;
		return;
	}
}

static void firmware_write(mem_t *mem, uint8_t v)
{
#if 0
	printf("[%08" PRIx32 "] SPI firmware write 0x%02" PRIx8 "\n",
	       cpu_get_reg(mem->nds->arm7, CPU_REG_PC), v);
#endif
	switch (mem->spi_firmware.cmd)
	{
		case 0x0:
			mem->spi_firmware.cmd = v;
			switch (v)
			{
				case 0x3:
					mem->spi_firmware.cmd_data.read.posb = 0;
					mem->spi_firmware.cmd_data.read.addr = 0;
					break;
				case 0x5:
					break;
				default:
					printf("unknown SPI firmware cmd: 0x%02" PRIx8 "\n", v);
					return;
			}
			return;
		case 0x3:
			if (mem->spi_firmware.cmd_data.read.posb < 3)
			{
				mem->spi_firmware.cmd_data.read.addr <<= 8;
				mem->spi_firmware.cmd_data.read.addr |= v;
				mem->spi_firmware.cmd_data.read.posb++;
				return;
			}
			mem->spi_firmware.read_latch = mem->firmware[mem->spi_firmware.cmd_data.read.addr & 0x3FFFF];
#if 0
			printf("[%08" PRIx32 "] firmware read: [%05" PRIx32 "] = %02" PRIx8 "\n",
			       cpu_get_reg(mem->nds->arm7, CPU_REG_PC),
			       mem->spi_firmware.cmd_data.read.addr,
			       mem->spi_firmware.read_latch);
#endif
#if 0
			if (mem->spi_firmware.cmd_data.read.addr == 0x1ef66)
				mem->nds->arm9->debug = CPU_DEBUG_ALL;
#endif
			mem->spi_firmware.cmd_data.read.addr++;
			return;
		case 0x5:
			mem->spi_firmware.read_latch = 0;
			return;
	}
}

static void touchscreen_write(mem_t *mem, uint8_t v)
{
#if 0
	printf("[%08" PRIx32 "] SPI touchscreen write 0x%02" PRIx8 "\n",
	       cpu_get_reg(mem->nds->arm7, CPU_REG_PC), v);
#endif
	if (v & (1 << 7))
	{
		mem->spi_touchscreen.channel = (v >> 4) & 0x7;
		mem->spi_touchscreen.has_channel = 1;
		return;
	}
	switch (mem->spi_touchscreen.channel)
	{
		case 0x1:
			if (mem->nds->touch)
				mem->spi_touchscreen.read_latch = 0xB0 + mem->nds->touch_y * 0x13;
			else
				mem->spi_touchscreen.read_latch = 0xFFF;
			mem->spi_touchscreen.read_pos = 0;
			break;
		case 0x5:
			if (mem->nds->touch)
				mem->spi_touchscreen.read_latch = 0x100 + mem->nds->touch_x * 0xE;
			else
				mem->spi_touchscreen.read_latch = 0x000;
			mem->spi_touchscreen.read_pos = 0;
			break;
		default:
#if 0
			printf("unknown touchscreen channel: %x\n",
			       mem->spi_touchscreen.channel);
#endif
			mem->spi_touchscreen.read_latch = 0x000;
			mem->spi_touchscreen.read_pos = 0;
			break;
	}
}

static void powerman_reset(mem_t *mem)
{
#if 0
	printf("[%08" PRIx32 "] SPI powerman reset\n",
	       cpu_get_reg(mem->nds->arm7, CPU_REG_PC));
#endif
	mem->spi_powerman.has_cmd = 0;
}

static void firmware_reset(mem_t *mem)
{
#if 0
	printf("[%08" PRIx32 "] SPI firmware reset\n",
	       cpu_get_reg(mem->nds->arm7, CPU_REG_PC));
#endif
	mem->spi_firmware.cmd = 0;
}

static void touchscreen_reset(mem_t *mem)
{
#if 0
	printf("[%08" PRIx32 "] SPI touchscreen reset\n",
	       cpu_get_reg(mem->nds->arm7, CPU_REG_PC));
#endif
	mem->spi_touchscreen.has_channel = 0;
}

static uint8_t spi_read(mem_t *mem)
{
#if 0
	printf("[%08" PRIx32 "] SPI read\n",
	       cpu_get_reg(mem->nds->arm7, CPU_REG_PC));
#endif
	switch (mem->arm7_regs[MEM_ARM7_REG_SPICNT + 1] & 0x3)
	{
		case 0:
			return powerman_read(mem);
		case 1:
			return firmware_read(mem);
		case 2:
			return touchscreen_read(mem);
		case 3:
			assert(!"invalid SPI device");
			return 0;
	}
	return 0;
}

static void spi_write(mem_t *mem, uint8_t v)
{
#if 0
	printf("[%08" PRIx32 "] SPI write %02" PRIx8 "\n",
	       cpu_get_reg(mem->nds->arm7, CPU_REG_PC), v);
#endif
	switch (mem->arm7_regs[MEM_ARM7_REG_SPICNT + 1] & 0x3)
	{
		case 0:
			powerman_write(mem, v);
			break;
		case 1:
			firmware_write(mem, v);
			break;
		case 2:
			touchscreen_write(mem, v);
			break;
		case 3:
			assert(!"invalid SPI device");
			return;
	}
	if (!(mem->arm7_regs[MEM_ARM7_REG_SPICNT + 1] & (1 << 3)))
	{
		switch (mem->arm7_regs[MEM_ARM7_REG_SPICNT + 1] & 0x3)
		{
			case 0:
				powerman_reset(mem);
				break;
			case 1:
				firmware_reset(mem);
				break;
			case 2:
				touchscreen_reset(mem);
				break;
		}
	}
	if (mem->arm7_regs[MEM_ARM7_REG_SPICNT + 1] & (1 << 6))
		mem_arm7_if(mem, 1 << 23);
}

/* the fact that every single RTC on earth uses BCD scares me */
#define BCD(n) (((n) % 10) + (((n) / 10) * 16))

static void rtc_write(mem_t *mem, uint8_t v)
{
#if 0
	printf("rtc write %02" PRIx8 "\n", v);
#endif
	if (v & (1 << 4))
	{
		if (!(v & (1 << 2)))
		{
#if 0
			printf("mem rtc buf reset\n");
#endif
			mem->rtc.inbuf = 0;
			mem->rtc.inlen = 0;
			mem->rtc.cmd_flip = 1;
			mem->rtc.cmd = 0xFF;
			mem->rtc.wpos = 0;
			return;
		}
		if (mem->rtc.cmd_flip)
		{
			mem->rtc.cmd_flip = 0;
			return;
		}
		if (!(v & (1 << 1)))
			return;
		mem->rtc.inbuf |= (v & 1) << (mem->rtc.inlen % 8);
		mem->rtc.inlen++;
		if (mem->rtc.inlen != 8)
			return;
		mem->rtc.inlen = 0;
		if (mem->rtc.cmd == 0xFF)
		{
			mem->rtc.cmd = mem->rtc.inbuf;
#if 0
			printf("rtc cmd %02" PRIx8 "\n", mem->rtc.cmd);
#endif
			if (mem->rtc.cmd & (1 << 7))
			{
				switch (mem->rtc.cmd)
				{
					case 0x86:
						mem->rtc.outbuf[0] = mem->rtc.sr1;
						mem->rtc.sr1 &= ~0xF0;
						mem->rtc.outpos = 0;
						mem->rtc.outlen = 8;
						break;
					case 0xC6:
						mem->rtc.outbuf[0] = mem->rtc.sr2;
						mem->rtc.outpos = 0;
						mem->rtc.outlen = 8;
						break;
					case 0xA6:
					{
						time_t t = time(NULL);
						struct tm *tm = localtime(&t);
						mem->rtc.outbuf[0] = BCD(tm->tm_year - 100);
						mem->rtc.outbuf[1] = BCD(tm->tm_mon + 1);
						mem->rtc.outbuf[2] = BCD(tm->tm_mday);
						mem->rtc.outbuf[3] = BCD(tm->tm_wday);
						mem->rtc.outbuf[4] = BCD(tm->tm_hour);
						mem->rtc.outbuf[5] = BCD(tm->tm_min);
						mem->rtc.outbuf[6] = BCD(tm->tm_sec);
#if 0
						printf("RTC read %x %x %x %x %x %x %x\n",
						       mem->rtc.outbuf[0],
						       mem->rtc.outbuf[1],
						       mem->rtc.outbuf[2],
						       mem->rtc.outbuf[3],
						       mem->rtc.outbuf[4],
						       mem->rtc.outbuf[5],
						       mem->rtc.outbuf[6]);
#endif
						mem->rtc.outpos = 0;
						mem->rtc.outlen = 8 * 7;
						break;
					}
					case 0xE6:
					{
						time_t t = time(NULL);
						struct tm *tm = localtime(&t);
						mem->rtc.outbuf[0] = BCD(tm->tm_hour);
						mem->rtc.outbuf[1] = BCD(tm->tm_min);
						mem->rtc.outbuf[2] = BCD(tm->tm_sec);
						mem->rtc.outpos = 0;
						mem->rtc.outlen = 8 * 3;
						break;
					}
					case 0x96:
						switch (mem->rtc.sr2 & 0xF)
						{
							case 0x1:
							case 0x5:
								mem->rtc.outbuf[0] = mem->rtc.int1_steady_freq;
								mem->rtc.outpos = 0;
								mem->rtc.outlen = 8;
								break;
							case 0x4:
								mem->rtc.outbuf[0] = mem->rtc.alarm1[0];
								mem->rtc.outbuf[1] = mem->rtc.alarm1[1];
								mem->rtc.outbuf[2] = mem->rtc.alarm1[2];
								mem->rtc.outpos = 0;
								mem->rtc.outlen = 8 * 3;
								break;
							default:
								printf("unknown rtc read sr2 pos: 0x%01" PRIx8 "\n",
								       mem->rtc.sr2 & 0xF);
								mem->rtc.outpos = 0;
								mem->rtc.outlen = 0;
								break;
						}
						break;
					case 0xD6:
						mem->rtc.outbuf[0] = mem->rtc.alarm2[0];
						mem->rtc.outbuf[1] = mem->rtc.alarm2[1];
						mem->rtc.outbuf[2] = mem->rtc.alarm2[2];
						mem->rtc.outpos = 0;
						mem->rtc.outlen = 8 * 3;
						break;
					case 0xB6:
						mem->rtc.outbuf[0] = mem->rtc.car;
						mem->rtc.outpos = 0;
						mem->rtc.outlen = 8;
						break;
					case 0xF6:
						mem->rtc.outbuf[0] = mem->rtc.fr;
						mem->rtc.outpos = 0;
						mem->rtc.outlen = 8;
						break;
					default:
						printf("unknown rtc read cmd: %02" PRIx8 "\n",
						       mem->rtc.cmd);
						break;
				}
			}
			return;
		}
		if (mem->rtc.cmd & (1 << 7))
		{
			printf("rtc write data on read cmd!\n");
			mem->rtc.inbuf = 0;
			return;
		}
#if 0
		printf("rtc byte %02" PRIx8 " for cmd %02" PRIx8 "\n",
		       mem->rtc.inbuf, mem->rtc.cmd);
#endif
		switch (mem->rtc.cmd)
		{
			case 0x06:
				mem->rtc.sr1 = mem->rtc.inbuf & 0x0E;
				break;
			case 0x46:
				mem->rtc.sr2 = mem->rtc.inbuf;
				break;
			case 0x26:
				printf("XXX rtc set date\n");
				break;
			case 0x66:
				printf("XXX rtc set time\n");
				break;
			case 0x16:
				switch (mem->rtc.sr2 & 0xF)
				{
					case 0x1:
					case 0x5:
						mem->rtc.int1_steady_freq = mem->rtc.inbuf;
						break;
					case 0x4:
						mem->rtc.alarm1[mem->rtc.wpos++] = mem->rtc.inbuf;
						break;
					default:
						printf("unknown rtc write sr2 pos: 0x%01" PRIx8 " = %02" PRIx8 "\n",
						       mem->rtc.sr2 & 0xF, mem->rtc.inbuf);
						break;
				}
				break;
			case 0x56:
				mem->rtc.alarm2[mem->rtc.wpos++] = mem->rtc.inbuf;
				break;
			case 0x36:
				mem->rtc.car = mem->rtc.inbuf;
				break;
			case 0x76:
				mem->rtc.fr = mem->rtc.inbuf;
				break;
			default:
				printf("unknown rtc write cmd: %02" PRIx8 "\n",
				       mem->rtc.cmd);
				break;
		}
		mem->rtc.inbuf = 0;
		return;
	}
	else
	{
		uint8_t b = 0;
#if 0
		printf("rtc read %u / %u\n", mem->rtc.outpos, mem->rtc.outlen);
#endif
		if (mem->rtc.outpos < mem->rtc.outlen)
		{
			b = mem->rtc.outbuf[mem->rtc.outpos / 8];
			b >>= mem->rtc.outpos % 8;
			b &= 1;
			if (v & (1 << 1))
				mem->rtc.outpos++;
		}
		mem->rtc.outbyte = 0x66 | b;
	}
}

static uint8_t rtc_read(mem_t *mem)
{
#if 0
	printf("[%08" PRIx32 "] rtc read %02" PRIx8 "\n",
	       cpu_get_reg(mem->nds->arm7, CPU_REG_PC), mem->rtc.outbyte);
#endif
	return mem->rtc.outbyte;
}

static void set_arm7_reg8(mem_t *mem, uint32_t addr, uint8_t v)
{
#if 0
	printf("ARM7 register [%08" PRIx32 "] = %02" PRIx8 "\n", addr, v);
#endif
	switch (addr)
	{
		case MEM_ARM7_REG_IPCSYNC:
			return;
		case MEM_ARM7_REG_IPCSYNC + 1:
			mem->arm7_regs[addr] = v & 0x47;
			if ((v & (1 << 5))
			 && (mem->arm9_regs[MEM_ARM9_REG_IPCSYNC + 1] & (1 << 6)))
				mem_arm9_if(mem, 1 << 16);
#if 0
			printf("ARM7 IPCSYNC write 0x%02" PRIx8 "\n", v);
#endif
			return;
		case MEM_ARM7_REG_IPCSYNC + 2:
		case MEM_ARM7_REG_IPCSYNC + 3:
			return;
		case MEM_ARM7_REG_IE:
		case MEM_ARM7_REG_IE + 1:
		case MEM_ARM7_REG_IE + 2:
		case MEM_ARM7_REG_IE + 3:
			mem->arm7_regs[addr] = v;
#if 0
			printf("[ARM7] IE 0x%08" PRIx32 "\n", mem_arm7_get_reg32(mem, MEM_ARM7_REG_IE));
#endif
			return;
		case MEM_ARM7_REG_IME:
		case MEM_ARM7_REG_IME + 1:
		case MEM_ARM7_REG_IME + 2:
		case MEM_ARM7_REG_IME + 3:
		case MEM_ARM7_REG_POSTFLG:
		case MEM_ARM7_REG_TM0CNT_L:
		case MEM_ARM7_REG_TM0CNT_L + 1:
		case MEM_ARM7_REG_TM0CNT_H + 1:
		case MEM_ARM7_REG_TM1CNT_L:
		case MEM_ARM7_REG_TM1CNT_L + 1:
		case MEM_ARM7_REG_TM1CNT_H + 1:
		case MEM_ARM7_REG_TM2CNT_L:
		case MEM_ARM7_REG_TM2CNT_L + 1:
		case MEM_ARM7_REG_TM2CNT_H + 1:
		case MEM_ARM7_REG_TM3CNT_L:
		case MEM_ARM7_REG_TM3CNT_L + 1:
		case MEM_ARM7_REG_TM3CNT_H + 1:
		case MEM_ARM7_REG_SOUNDBIAS:
		case MEM_ARM7_REG_SOUNDBIAS + 1:
		case MEM_ARM7_REG_SOUNDBIAS + 2:
		case MEM_ARM7_REG_SOUNDBIAS + 3:
		case MEM_ARM7_REG_DMA0SAD:
		case MEM_ARM7_REG_DMA0SAD + 1:
		case MEM_ARM7_REG_DMA0SAD + 2:
		case MEM_ARM7_REG_DMA0SAD + 3:
		case MEM_ARM7_REG_DMA0DAD:
		case MEM_ARM7_REG_DMA0DAD + 1:
		case MEM_ARM7_REG_DMA0DAD + 2:
		case MEM_ARM7_REG_DMA0DAD + 3:
		case MEM_ARM7_REG_DMA0CNT_L:
		case MEM_ARM7_REG_DMA0CNT_L + 1:
		case MEM_ARM7_REG_DMA0CNT_H:
		case MEM_ARM7_REG_DMA1SAD:
		case MEM_ARM7_REG_DMA1SAD + 1:
		case MEM_ARM7_REG_DMA1SAD + 2:
		case MEM_ARM7_REG_DMA1SAD + 3:
		case MEM_ARM7_REG_DMA1DAD:
		case MEM_ARM7_REG_DMA1DAD + 1:
		case MEM_ARM7_REG_DMA1DAD + 2:
		case MEM_ARM7_REG_DMA1DAD + 3:
		case MEM_ARM7_REG_DMA1CNT_L:
		case MEM_ARM7_REG_DMA1CNT_L + 1:
		case MEM_ARM7_REG_DMA1CNT_H:
		case MEM_ARM7_REG_DMA2SAD:
		case MEM_ARM7_REG_DMA2SAD + 1:
		case MEM_ARM7_REG_DMA2SAD + 2:
		case MEM_ARM7_REG_DMA2SAD + 3:
		case MEM_ARM7_REG_DMA2DAD:
		case MEM_ARM7_REG_DMA2DAD + 1:
		case MEM_ARM7_REG_DMA2DAD + 2:
		case MEM_ARM7_REG_DMA2DAD + 3:
		case MEM_ARM7_REG_DMA2CNT_L:
		case MEM_ARM7_REG_DMA2CNT_L + 1:
		case MEM_ARM7_REG_DMA2CNT_H:
		case MEM_ARM7_REG_DMA3SAD:
		case MEM_ARM7_REG_DMA3SAD + 1:
		case MEM_ARM7_REG_DMA3SAD + 2:
		case MEM_ARM7_REG_DMA3SAD + 3:
		case MEM_ARM7_REG_DMA3DAD:
		case MEM_ARM7_REG_DMA3DAD + 1:
		case MEM_ARM7_REG_DMA3DAD + 2:
		case MEM_ARM7_REG_DMA3DAD + 3:
		case MEM_ARM7_REG_DMA3CNT_L:
		case MEM_ARM7_REG_DMA3CNT_L  +1:
		case MEM_ARM7_REG_DMA3CNT_H:
		case MEM_ARM7_REG_POWCNT2:
		case MEM_ARM7_REG_POWCNT2 + 1:
		case MEM_ARM7_REG_POWCNT2 + 2:
		case MEM_ARM7_REG_POWCNT2 + 3:
		case MEM_ARM7_REG_RCNT:
		case MEM_ARM7_REG_RCNT + 1:
		case MEM_ARM7_REG_SOUNDCNT:
		case MEM_ARM7_REG_SOUNDCNT + 1:
		case MEM_ARM7_REG_WIFIWAITCNT:
		case MEM_ARM7_REG_WIFIWAITCNT + 1:
		case MEM_ARM7_REG_SNDCAP0CNT:
		case MEM_ARM7_REG_SNDCAP0DAD:
		case MEM_ARM7_REG_SNDCAP0DAD + 1:
		case MEM_ARM7_REG_SNDCAP0DAD + 2:
		case MEM_ARM7_REG_SNDCAP0DAD + 3:
		case MEM_ARM7_REG_SNDCAP0LEN:
		case MEM_ARM7_REG_SNDCAP0LEN + 1:
		case MEM_ARM7_REG_SNDCAP0LEN + 2:
		case MEM_ARM7_REG_SNDCAP0LEN + 3:
		case MEM_ARM7_REG_SNDCAP1CNT:
		case MEM_ARM7_REG_SNDCAP1DAD:
		case MEM_ARM7_REG_SNDCAP1DAD + 1:
		case MEM_ARM7_REG_SNDCAP1DAD + 2:
		case MEM_ARM7_REG_SNDCAP1DAD + 3:
		case MEM_ARM7_REG_SNDCAP1LEN:
		case MEM_ARM7_REG_SNDCAP1LEN + 1:
		case MEM_ARM7_REG_SNDCAP1LEN + 2:
		case MEM_ARM7_REG_SNDCAP1LEN + 3:
			mem->arm7_regs[addr] = v;
			return;
		case MEM_ARM7_REG_SOUNDXCNT(0):
		case MEM_ARM7_REG_SOUNDXCNT(0) + 1:
		case MEM_ARM7_REG_SOUNDXCNT(0) + 2:
		case MEM_ARM7_REG_SOUNDXCNT(1):
		case MEM_ARM7_REG_SOUNDXCNT(1) + 1:
		case MEM_ARM7_REG_SOUNDXCNT(1) + 2:
		case MEM_ARM7_REG_SOUNDXCNT(2):
		case MEM_ARM7_REG_SOUNDXCNT(2) + 1:
		case MEM_ARM7_REG_SOUNDXCNT(2) + 2:
		case MEM_ARM7_REG_SOUNDXCNT(3):
		case MEM_ARM7_REG_SOUNDXCNT(3) + 1:
		case MEM_ARM7_REG_SOUNDXCNT(3) + 2:
		case MEM_ARM7_REG_SOUNDXCNT(4):
		case MEM_ARM7_REG_SOUNDXCNT(4) + 1:
		case MEM_ARM7_REG_SOUNDXCNT(4) + 2:
		case MEM_ARM7_REG_SOUNDXCNT(5):
		case MEM_ARM7_REG_SOUNDXCNT(5) + 1:
		case MEM_ARM7_REG_SOUNDXCNT(5) + 2:
		case MEM_ARM7_REG_SOUNDXCNT(6):
		case MEM_ARM7_REG_SOUNDXCNT(6) + 1:
		case MEM_ARM7_REG_SOUNDXCNT(6) + 2:
		case MEM_ARM7_REG_SOUNDXCNT(7):
		case MEM_ARM7_REG_SOUNDXCNT(7) + 1:
		case MEM_ARM7_REG_SOUNDXCNT(7) + 2:
		case MEM_ARM7_REG_SOUNDXCNT(8):
		case MEM_ARM7_REG_SOUNDXCNT(8) + 1:
		case MEM_ARM7_REG_SOUNDXCNT(8) + 2:
		case MEM_ARM7_REG_SOUNDXCNT(9):
		case MEM_ARM7_REG_SOUNDXCNT(9) + 1:
		case MEM_ARM7_REG_SOUNDXCNT(9) + 2:
		case MEM_ARM7_REG_SOUNDXCNT(10):
		case MEM_ARM7_REG_SOUNDXCNT(10) + 1:
		case MEM_ARM7_REG_SOUNDXCNT(10) + 2:
		case MEM_ARM7_REG_SOUNDXCNT(11):
		case MEM_ARM7_REG_SOUNDXCNT(11) + 1:
		case MEM_ARM7_REG_SOUNDXCNT(11) + 2:
		case MEM_ARM7_REG_SOUNDXCNT(12):
		case MEM_ARM7_REG_SOUNDXCNT(12) + 1:
		case MEM_ARM7_REG_SOUNDXCNT(12) + 2:
		case MEM_ARM7_REG_SOUNDXCNT(13):
		case MEM_ARM7_REG_SOUNDXCNT(13) + 1:
		case MEM_ARM7_REG_SOUNDXCNT(13) + 2:
		case MEM_ARM7_REG_SOUNDXCNT(14):
		case MEM_ARM7_REG_SOUNDXCNT(14) + 1:
		case MEM_ARM7_REG_SOUNDXCNT(14) + 2:
		case MEM_ARM7_REG_SOUNDXCNT(15):
		case MEM_ARM7_REG_SOUNDXCNT(15) + 1:
		case MEM_ARM7_REG_SOUNDXCNT(15) + 2:
		case MEM_ARM7_REG_SOUNDXSAD(0):
		case MEM_ARM7_REG_SOUNDXSAD(0) + 1:
		case MEM_ARM7_REG_SOUNDXSAD(0) + 2:
		case MEM_ARM7_REG_SOUNDXSAD(0) + 3:
		case MEM_ARM7_REG_SOUNDXSAD(1):
		case MEM_ARM7_REG_SOUNDXSAD(1) + 1:
		case MEM_ARM7_REG_SOUNDXSAD(1) + 2:
		case MEM_ARM7_REG_SOUNDXSAD(1) + 3:
		case MEM_ARM7_REG_SOUNDXSAD(2):
		case MEM_ARM7_REG_SOUNDXSAD(2) + 1:
		case MEM_ARM7_REG_SOUNDXSAD(2) + 2:
		case MEM_ARM7_REG_SOUNDXSAD(2) + 3:
		case MEM_ARM7_REG_SOUNDXSAD(3):
		case MEM_ARM7_REG_SOUNDXSAD(3) + 1:
		case MEM_ARM7_REG_SOUNDXSAD(3) + 2:
		case MEM_ARM7_REG_SOUNDXSAD(3) + 3:
		case MEM_ARM7_REG_SOUNDXSAD(4):
		case MEM_ARM7_REG_SOUNDXSAD(4) + 1:
		case MEM_ARM7_REG_SOUNDXSAD(4) + 2:
		case MEM_ARM7_REG_SOUNDXSAD(4) + 3:
		case MEM_ARM7_REG_SOUNDXSAD(5):
		case MEM_ARM7_REG_SOUNDXSAD(5) + 1:
		case MEM_ARM7_REG_SOUNDXSAD(5) + 2:
		case MEM_ARM7_REG_SOUNDXSAD(5) + 3:
		case MEM_ARM7_REG_SOUNDXSAD(6):
		case MEM_ARM7_REG_SOUNDXSAD(6) + 1:
		case MEM_ARM7_REG_SOUNDXSAD(6) + 2:
		case MEM_ARM7_REG_SOUNDXSAD(6) + 3:
		case MEM_ARM7_REG_SOUNDXSAD(7):
		case MEM_ARM7_REG_SOUNDXSAD(7) + 1:
		case MEM_ARM7_REG_SOUNDXSAD(7) + 2:
		case MEM_ARM7_REG_SOUNDXSAD(7) + 3:
		case MEM_ARM7_REG_SOUNDXSAD(8):
		case MEM_ARM7_REG_SOUNDXSAD(8) + 1:
		case MEM_ARM7_REG_SOUNDXSAD(8) + 2:
		case MEM_ARM7_REG_SOUNDXSAD(8) + 3:
		case MEM_ARM7_REG_SOUNDXSAD(9):
		case MEM_ARM7_REG_SOUNDXSAD(9) + 1:
		case MEM_ARM7_REG_SOUNDXSAD(9) + 2:
		case MEM_ARM7_REG_SOUNDXSAD(9) + 3:
		case MEM_ARM7_REG_SOUNDXSAD(10):
		case MEM_ARM7_REG_SOUNDXSAD(10) + 1:
		case MEM_ARM7_REG_SOUNDXSAD(10) + 2:
		case MEM_ARM7_REG_SOUNDXSAD(10) + 3:
		case MEM_ARM7_REG_SOUNDXSAD(11):
		case MEM_ARM7_REG_SOUNDXSAD(11) + 1:
		case MEM_ARM7_REG_SOUNDXSAD(11) + 2:
		case MEM_ARM7_REG_SOUNDXSAD(11) + 3:
		case MEM_ARM7_REG_SOUNDXSAD(12):
		case MEM_ARM7_REG_SOUNDXSAD(12) + 1:
		case MEM_ARM7_REG_SOUNDXSAD(12) + 2:
		case MEM_ARM7_REG_SOUNDXSAD(12) + 3:
		case MEM_ARM7_REG_SOUNDXSAD(13):
		case MEM_ARM7_REG_SOUNDXSAD(13) + 1:
		case MEM_ARM7_REG_SOUNDXSAD(13) + 2:
		case MEM_ARM7_REG_SOUNDXSAD(13) + 3:
		case MEM_ARM7_REG_SOUNDXSAD(14):
		case MEM_ARM7_REG_SOUNDXSAD(14) + 1:
		case MEM_ARM7_REG_SOUNDXSAD(14) + 2:
		case MEM_ARM7_REG_SOUNDXSAD(14) + 3:
		case MEM_ARM7_REG_SOUNDXSAD(15):
		case MEM_ARM7_REG_SOUNDXSAD(15) + 1:
		case MEM_ARM7_REG_SOUNDXSAD(15) + 2:
		case MEM_ARM7_REG_SOUNDXSAD(15) + 3:
		case MEM_ARM7_REG_SOUNDXTMR(0):
		case MEM_ARM7_REG_SOUNDXTMR(0) + 1:
		case MEM_ARM7_REG_SOUNDXTMR(1):
		case MEM_ARM7_REG_SOUNDXTMR(1) + 1:
		case MEM_ARM7_REG_SOUNDXTMR(2):
		case MEM_ARM7_REG_SOUNDXTMR(2) + 1:
		case MEM_ARM7_REG_SOUNDXTMR(3):
		case MEM_ARM7_REG_SOUNDXTMR(3) + 1:
		case MEM_ARM7_REG_SOUNDXTMR(4):
		case MEM_ARM7_REG_SOUNDXTMR(4) + 1:
		case MEM_ARM7_REG_SOUNDXTMR(5):
		case MEM_ARM7_REG_SOUNDXTMR(5) + 1:
		case MEM_ARM7_REG_SOUNDXTMR(6):
		case MEM_ARM7_REG_SOUNDXTMR(6) + 1:
		case MEM_ARM7_REG_SOUNDXTMR(7):
		case MEM_ARM7_REG_SOUNDXTMR(7) + 1:
		case MEM_ARM7_REG_SOUNDXTMR(8):
		case MEM_ARM7_REG_SOUNDXTMR(8) + 1:
		case MEM_ARM7_REG_SOUNDXTMR(9):
		case MEM_ARM7_REG_SOUNDXTMR(9) + 1:
		case MEM_ARM7_REG_SOUNDXTMR(10):
		case MEM_ARM7_REG_SOUNDXTMR(10) + 1:
		case MEM_ARM7_REG_SOUNDXTMR(11):
		case MEM_ARM7_REG_SOUNDXTMR(11) + 1:
		case MEM_ARM7_REG_SOUNDXTMR(12):
		case MEM_ARM7_REG_SOUNDXTMR(12) + 1:
		case MEM_ARM7_REG_SOUNDXTMR(13):
		case MEM_ARM7_REG_SOUNDXTMR(13) + 1:
		case MEM_ARM7_REG_SOUNDXTMR(14):
		case MEM_ARM7_REG_SOUNDXTMR(14) + 1:
		case MEM_ARM7_REG_SOUNDXTMR(15):
		case MEM_ARM7_REG_SOUNDXTMR(15) + 1:
		case MEM_ARM7_REG_SOUNDXPNT(0):
		case MEM_ARM7_REG_SOUNDXPNT(0) + 1:
		case MEM_ARM7_REG_SOUNDXPNT(1):
		case MEM_ARM7_REG_SOUNDXPNT(1) + 1:
		case MEM_ARM7_REG_SOUNDXPNT(2):
		case MEM_ARM7_REG_SOUNDXPNT(2) + 1:
		case MEM_ARM7_REG_SOUNDXPNT(3):
		case MEM_ARM7_REG_SOUNDXPNT(3) + 1:
		case MEM_ARM7_REG_SOUNDXPNT(4):
		case MEM_ARM7_REG_SOUNDXPNT(4) + 1:
		case MEM_ARM7_REG_SOUNDXPNT(5):
		case MEM_ARM7_REG_SOUNDXPNT(5) + 1:
		case MEM_ARM7_REG_SOUNDXPNT(6):
		case MEM_ARM7_REG_SOUNDXPNT(6) + 1:
		case MEM_ARM7_REG_SOUNDXPNT(7):
		case MEM_ARM7_REG_SOUNDXPNT(7) + 1:
		case MEM_ARM7_REG_SOUNDXPNT(8):
		case MEM_ARM7_REG_SOUNDXPNT(8) + 1:
		case MEM_ARM7_REG_SOUNDXPNT(9):
		case MEM_ARM7_REG_SOUNDXPNT(9) + 1:
		case MEM_ARM7_REG_SOUNDXPNT(10):
		case MEM_ARM7_REG_SOUNDXPNT(10) + 1:
		case MEM_ARM7_REG_SOUNDXPNT(11):
		case MEM_ARM7_REG_SOUNDXPNT(11) + 1:
		case MEM_ARM7_REG_SOUNDXPNT(12):
		case MEM_ARM7_REG_SOUNDXPNT(12) + 1:
		case MEM_ARM7_REG_SOUNDXPNT(13):
		case MEM_ARM7_REG_SOUNDXPNT(13) + 1:
		case MEM_ARM7_REG_SOUNDXPNT(14):
		case MEM_ARM7_REG_SOUNDXPNT(14) + 1:
		case MEM_ARM7_REG_SOUNDXPNT(15):
		case MEM_ARM7_REG_SOUNDXPNT(15) + 1:
		case MEM_ARM7_REG_SOUNDXLEN(0):
		case MEM_ARM7_REG_SOUNDXLEN(0) + 1:
		case MEM_ARM7_REG_SOUNDXLEN(0) + 2:
		case MEM_ARM7_REG_SOUNDXLEN(0) + 3:
		case MEM_ARM7_REG_SOUNDXLEN(1):
		case MEM_ARM7_REG_SOUNDXLEN(1) + 1:
		case MEM_ARM7_REG_SOUNDXLEN(1) + 2:
		case MEM_ARM7_REG_SOUNDXLEN(1) + 3:
		case MEM_ARM7_REG_SOUNDXLEN(2):
		case MEM_ARM7_REG_SOUNDXLEN(2) + 1:
		case MEM_ARM7_REG_SOUNDXLEN(2) + 2:
		case MEM_ARM7_REG_SOUNDXLEN(2) + 3:
		case MEM_ARM7_REG_SOUNDXLEN(3):
		case MEM_ARM7_REG_SOUNDXLEN(3) + 1:
		case MEM_ARM7_REG_SOUNDXLEN(3) + 2:
		case MEM_ARM7_REG_SOUNDXLEN(3) + 3:
		case MEM_ARM7_REG_SOUNDXLEN(4):
		case MEM_ARM7_REG_SOUNDXLEN(4) + 1:
		case MEM_ARM7_REG_SOUNDXLEN(4) + 2:
		case MEM_ARM7_REG_SOUNDXLEN(4) + 3:
		case MEM_ARM7_REG_SOUNDXLEN(5):
		case MEM_ARM7_REG_SOUNDXLEN(5) + 1:
		case MEM_ARM7_REG_SOUNDXLEN(5) + 2:
		case MEM_ARM7_REG_SOUNDXLEN(5) + 3:
		case MEM_ARM7_REG_SOUNDXLEN(6):
		case MEM_ARM7_REG_SOUNDXLEN(6) + 1:
		case MEM_ARM7_REG_SOUNDXLEN(6) + 2:
		case MEM_ARM7_REG_SOUNDXLEN(6) + 3:
		case MEM_ARM7_REG_SOUNDXLEN(7):
		case MEM_ARM7_REG_SOUNDXLEN(7) + 1:
		case MEM_ARM7_REG_SOUNDXLEN(7) + 2:
		case MEM_ARM7_REG_SOUNDXLEN(7) + 3:
		case MEM_ARM7_REG_SOUNDXLEN(8):
		case MEM_ARM7_REG_SOUNDXLEN(8) + 1:
		case MEM_ARM7_REG_SOUNDXLEN(8) + 2:
		case MEM_ARM7_REG_SOUNDXLEN(8) + 3:
		case MEM_ARM7_REG_SOUNDXLEN(9):
		case MEM_ARM7_REG_SOUNDXLEN(9) + 1:
		case MEM_ARM7_REG_SOUNDXLEN(9) + 2:
		case MEM_ARM7_REG_SOUNDXLEN(9) + 3:
		case MEM_ARM7_REG_SOUNDXLEN(10):
		case MEM_ARM7_REG_SOUNDXLEN(10) + 1:
		case MEM_ARM7_REG_SOUNDXLEN(10) + 2:
		case MEM_ARM7_REG_SOUNDXLEN(10) + 3:
		case MEM_ARM7_REG_SOUNDXLEN(11):
		case MEM_ARM7_REG_SOUNDXLEN(11) + 1:
		case MEM_ARM7_REG_SOUNDXLEN(11) + 2:
		case MEM_ARM7_REG_SOUNDXLEN(11) + 3:
		case MEM_ARM7_REG_SOUNDXLEN(12):
		case MEM_ARM7_REG_SOUNDXLEN(12) + 1:
		case MEM_ARM7_REG_SOUNDXLEN(12) + 2:
		case MEM_ARM7_REG_SOUNDXLEN(12) + 3:
		case MEM_ARM7_REG_SOUNDXLEN(13):
		case MEM_ARM7_REG_SOUNDXLEN(13) + 1:
		case MEM_ARM7_REG_SOUNDXLEN(13) + 2:
		case MEM_ARM7_REG_SOUNDXLEN(13) + 3:
		case MEM_ARM7_REG_SOUNDXLEN(14):
		case MEM_ARM7_REG_SOUNDXLEN(14) + 1:
		case MEM_ARM7_REG_SOUNDXLEN(14) + 2:
		case MEM_ARM7_REG_SOUNDXLEN(14) + 3:
		case MEM_ARM7_REG_SOUNDXLEN(15):
		case MEM_ARM7_REG_SOUNDXLEN(15) + 1:
		case MEM_ARM7_REG_SOUNDXLEN(15) + 2:
		case MEM_ARM7_REG_SOUNDXLEN(15) + 3:
#if 0
			printf("SND[%08" PRIx32 "] = %02" PRIx8 "\n", addr, v);
#endif
			mem->arm7_regs[addr] = v;
			return;
		case MEM_ARM7_REG_SOUNDXCNT(0) + 3:
		case MEM_ARM7_REG_SOUNDXCNT(1) + 3:
		case MEM_ARM7_REG_SOUNDXCNT(2) + 3:
		case MEM_ARM7_REG_SOUNDXCNT(3) + 3:
		case MEM_ARM7_REG_SOUNDXCNT(4) + 3:
		case MEM_ARM7_REG_SOUNDXCNT(5) + 3:
		case MEM_ARM7_REG_SOUNDXCNT(6) + 3:
		case MEM_ARM7_REG_SOUNDXCNT(7) + 3:
		case MEM_ARM7_REG_SOUNDXCNT(8) + 3:
		case MEM_ARM7_REG_SOUNDXCNT(9) + 3:
		case MEM_ARM7_REG_SOUNDXCNT(10) + 3:
		case MEM_ARM7_REG_SOUNDXCNT(11) + 3:
		case MEM_ARM7_REG_SOUNDXCNT(12) + 3:
		case MEM_ARM7_REG_SOUNDXCNT(13) + 3:
		case MEM_ARM7_REG_SOUNDXCNT(14) + 3:
		case MEM_ARM7_REG_SOUNDXCNT(15) + 3:
		{
			bool start = ((v & (1 << 7)) != (mem->arm7_regs[addr] & (1 << 7)));
			mem->arm7_regs[addr] = v;
			if (start)
				apu_start_channel(mem->nds->apu, (addr - (MEM_ARM7_REG_SOUNDXCNT(0) + 3)) / 0x10);
			return;
		}
		case MEM_ARM7_REG_ROMCTRL:
		case MEM_ARM7_REG_ROMCTRL + 1:
		case MEM_ARM7_REG_ROMCMD:
		case MEM_ARM7_REG_ROMCMD + 1:
		case MEM_ARM7_REG_ROMCMD + 2:
		case MEM_ARM7_REG_ROMCMD + 3:
		case MEM_ARM7_REG_ROMCMD + 4:
		case MEM_ARM7_REG_ROMCMD + 5:
		case MEM_ARM7_REG_ROMCMD + 6:
		case MEM_ARM7_REG_ROMCMD + 7:
		case MEM_ARM7_REG_DISPSTAT:
		case MEM_ARM7_REG_DISPSTAT + 1:
			mem->arm9_regs[addr] = v;
			return;
		case MEM_ARM7_REG_AUXSPICNT:
#if 1
			printf("[ARM7] AUXSPICNT[%08" PRIx32 "] = %02" PRIx8 "\n",
			       addr, v);
#endif
			mem->arm9_regs[addr] = v & ~(1 << 7);
			return;
		case MEM_ARM7_REG_AUXSPICNT + 1:
#if 1
			printf("[ARM7] AUXSPICNT[%08" PRIx32 "] = %02" PRIx8 "\n",
			       addr, v);
#endif
			mem->arm9_regs[addr] = v;
			return;
		case MEM_ARM7_REG_AUXSPIDATA:
			mbc_spi_write(mem->mbc, v);
			return;
		case MEM_ARM7_REG_ROMDATA:
		case MEM_ARM7_REG_ROMDATA + 1:
		case MEM_ARM7_REG_ROMDATA + 2:
		case MEM_ARM7_REG_ROMDATA + 3:
#if 0
			printf("[ARM7] MBC write %02" PRIx8 "\n", v);
#endif
			mbc_write(mem->mbc, v);
			return;
		case MEM_ARM7_REG_ROMCTRL + 2:
			mem->arm9_regs[addr] = (mem->arm9_regs[addr] & (1 << 7))
			                     | (v & ~(1 << 7));
			return;
		case MEM_ARM7_REG_ROMCTRL + 3:
			mem->arm9_regs[addr] = v;
			if (v & 0x80)
			{
#if 0
				printf("[ARM9] MBC cmd %02" PRIx8 "\n", v);
#endif
				mbc_cmd(mem->mbc);
			}
			return;
		case MEM_ARM7_REG_IF:
		case MEM_ARM7_REG_IF + 1:
		case MEM_ARM7_REG_IF + 2:
		case MEM_ARM7_REG_IF + 3:
			mem->arm7_regs[addr] &= ~v;
			return;
		case MEM_ARM7_REG_TM0CNT_H:
			arm7_timer_control(mem, 0, v);
			return;
		case MEM_ARM7_REG_TM1CNT_H:
			arm7_timer_control(mem, 1, v);
			return;
		case MEM_ARM7_REG_TM2CNT_H:
			arm7_timer_control(mem, 2, v);
			return;
		case MEM_ARM7_REG_TM3CNT_H:
			arm7_timer_control(mem, 3, v);
			return;
		case MEM_ARM7_REG_SPICNT:
#if 0
			printf("[%08" PRIx32 "] SPICNT[%08" PRIx32 "] = %02" PRIx8 "\n",
			       cpu_get_reg(mem->nds->arm7, CPU_REG_PC), addr, v);
#endif
			mem->arm7_regs[addr] = v & ~(1 << 7);
			return;
		case MEM_ARM7_REG_SPICNT + 1:
#if 0
			printf("[%08" PRIx32 "] SPICNT[%08" PRIx32 "] = %02" PRIx8 "\n",
			       cpu_get_reg(mem->nds->arm7, CPU_REG_PC), addr, v);
#endif
			mem->arm7_regs[addr] = v;
			return;
		case MEM_ARM7_REG_SPIDATA:
			spi_write(mem, v);
			return;
		case MEM_ARM7_REG_HALTCNT:
			switch ((v >> 6) & 0x3)
			{
				case 0:
					return;
				case 1:
					assert(!"GBA mode not supported");
					return;
				case 2:
					mem->nds->arm7->state = CPU_STATE_HALT;
					return;
				case 3:
					mem->nds->arm7->state = CPU_STATE_STOP;
					return;
			}
			return;
		case MEM_ARM7_REG_BIOSPROT:
		case MEM_ARM7_REG_BIOSPROT + 1:
		case MEM_ARM7_REG_BIOSPROT + 2:
			if (!mem->biosprot)
				mem->arm7_regs[addr] = v;
			return;
		case MEM_ARM7_REG_BIOSPROT + 3:
			if (!mem->biosprot)
				mem->arm7_regs[addr] = v;
			mem->biosprot = 1;
			return;
		case MEM_ARM7_REG_RTC:
			rtc_write(mem, v);
			return;
		case MEM_ARM7_REG_DMA0CNT_H + 1:
			mem->arm7_regs[addr] = v;
			arm7_dma_control(mem, 0);
			return;
		case MEM_ARM7_REG_DMA1CNT_H + 1:
			mem->arm7_regs[addr] = v;
			arm7_dma_control(mem, 1);
			return;
		case MEM_ARM7_REG_DMA2CNT_H + 1:
			mem->arm7_regs[addr] = v;
			arm7_dma_control(mem, 2);
			return;
		case MEM_ARM7_REG_DMA3CNT_H + 1:
			mem->arm7_regs[addr] = v;
			arm7_dma_control(mem, 3);
			return;
		case MEM_ARM7_REG_SPIDATA + 1:
		case MEM_ARM7_REG_RTC + 1:
		case MEM_ARM7_REG_RTC + 2:
		case MEM_ARM7_REG_RTC + 3:
		case MEM_ARM7_REG_WRAMSTAT:
		case MEM_ARM7_REG_SIODATA32:
		case MEM_ARM7_REG_SIODATA32 + 1:
		case MEM_ARM7_REG_SIODATA32 + 2:
		case MEM_ARM7_REG_SIODATA32 + 3:
		case MEM_ARM7_REG_SIOCNT:
		case MEM_ARM7_REG_SIOCNT + 1:
		case MEM_ARM7_REG_SIOCNT + 2:
		case MEM_ARM7_REG_SIOCNT + 3:
		case MEM_ARM7_REG_EXMEMSTAT:
		case MEM_ARM7_REG_EXMEMSTAT + 1:
		case MEM_ARM7_REG_IPCFIFOCNT + 2:
		case MEM_ARM7_REG_IPCFIFOCNT + 3:
			return;
		case MEM_ARM7_REG_KEYCNT:
		case MEM_ARM7_REG_KEYCNT + 1:
			mem->arm7_regs[addr] = v;
			nds_test_keypad_int(mem->nds);
			return;
		case MEM_ARM7_REG_IPCFIFOCNT:
#if 0
			printf("ARM7 FIFOCNT[0] write %02" PRIx8 "\n", v);
#endif
			if (v & (1 << 3))
			{
#if 0
				printf("ARM9 FIFO clean\n");
#endif
				mem->arm9_fifo.len = 0;
				mem->arm9_fifo.latch[0] = 0;
				mem->arm9_fifo.latch[1] = 0;
				mem->arm9_fifo.latch[2] = 0;
				mem->arm9_fifo.latch[3] = 0;
			}
			mem->arm7_regs[addr] &= ~(1 << 2);
			mem->arm7_regs[addr] |= v & (1 << 2);
			return;
		case MEM_ARM7_REG_IPCFIFOCNT + 1:
#if 0
			printf("ARM7 FIFOCNT[1] write %02" PRIx8 "\n", v);
#endif
			mem->arm7_regs[addr] &= ~0x84;
			mem->arm7_regs[addr] |= v & 0x84;
			if (v & (1 << 6))
				mem->arm7_regs[addr] &= ~(1 << 6);
			return;
		case MEM_ARM7_REG_IPCFIFOSEND:
		case MEM_ARM7_REG_IPCFIFOSEND + 1:
		case MEM_ARM7_REG_IPCFIFOSEND + 2:
		case MEM_ARM7_REG_IPCFIFOSEND + 3:
			if (!(mem->arm9_regs[MEM_ARM9_REG_IPCFIFOCNT + 1] & (1 << 7)))
				return;
			if (mem->arm9_fifo.len == 64)
			{
#if 0
				printf("ARM9 FIFO full\n");
#endif
				mem->arm7_regs[MEM_ARM7_REG_IPCFIFOCNT + 1] |= (1 << 6);
				return;
			}
			mem->arm9_fifo.data[(mem->arm9_fifo.pos + mem->arm9_fifo.len) % 64] = v;
			mem->arm9_fifo.len++;
			if (mem->arm9_fifo.len == 4
			 && (mem->arm9_regs[MEM_ARM9_REG_IPCFIFOCNT + 1] & (1 << 2)))
				mem_arm9_if(mem, 1 << 18);
#if 0
			printf("ARM7 IPCFIFO write (now %" PRIu8 ")\n", mem->arm9_fifo.len);
#endif
			return;
		default:
			printf("[%08" PRIx32 "] unknown ARM7 set register %08" PRIx32 " = %02" PRIx8 "\n",
			       cpu_get_reg(mem->nds->arm7, CPU_REG_PC), addr, v);
			break;
	}
}

static void set_arm7_reg16(mem_t *mem, uint32_t addr, uint16_t v)
{
	set_arm7_reg8(mem, addr + 0, v >> 0);
	set_arm7_reg8(mem, addr + 1, v >> 8);
}

static void set_arm7_reg32(mem_t *mem, uint32_t addr, uint32_t v)
{
	set_arm7_reg8(mem, addr + 0, v >> 0);
	set_arm7_reg8(mem, addr + 1, v >> 8);
	set_arm7_reg8(mem, addr + 2, v >> 16);
	set_arm7_reg8(mem, addr + 3, v >> 24);
}

static uint8_t get_arm7_reg8(mem_t *mem, uint32_t addr)
{
	switch (addr)
	{
		case MEM_ARM7_REG_IPCSYNC:
			return mem->arm9_regs[MEM_ARM9_REG_IPCSYNC + 1] & 0x7;
		case MEM_ARM7_REG_IPCSYNC + 1:
		case MEM_ARM7_REG_IPCSYNC + 2:
		case MEM_ARM7_REG_IPCSYNC + 3:
		case MEM_ARM7_REG_IE:
		case MEM_ARM7_REG_IE + 1:
		case MEM_ARM7_REG_IE + 2:
		case MEM_ARM7_REG_IE + 3:
		case MEM_ARM7_REG_IF:
		case MEM_ARM7_REG_IF + 1:
		case MEM_ARM7_REG_IF + 2:
		case MEM_ARM7_REG_IF + 3:
		case MEM_ARM7_REG_IME:
		case MEM_ARM7_REG_IME + 1:
		case MEM_ARM7_REG_IME + 2:
		case MEM_ARM7_REG_IME + 3:
		case MEM_ARM7_REG_POSTFLG:
		case MEM_ARM7_REG_HALTCNT:
		case MEM_ARM7_REG_BIOSPROT:
		case MEM_ARM7_REG_BIOSPROT + 1:
		case MEM_ARM7_REG_BIOSPROT + 2:
		case MEM_ARM7_REG_BIOSPROT + 3:
		case MEM_ARM7_REG_SOUNDBIAS:
		case MEM_ARM7_REG_SOUNDBIAS + 1:
		case MEM_ARM7_REG_SOUNDBIAS + 2:
		case MEM_ARM7_REG_SOUNDBIAS + 3:
		case MEM_ARM7_REG_DMA0SAD:
		case MEM_ARM7_REG_DMA0SAD + 1:
		case MEM_ARM7_REG_DMA0SAD + 2:
		case MEM_ARM7_REG_DMA0SAD + 3:
		case MEM_ARM7_REG_DMA0DAD:
		case MEM_ARM7_REG_DMA0DAD + 1:
		case MEM_ARM7_REG_DMA0DAD + 2:
		case MEM_ARM7_REG_DMA0DAD + 3:
		case MEM_ARM7_REG_DMA0CNT_L:
		case MEM_ARM7_REG_DMA0CNT_L + 1:
		case MEM_ARM7_REG_DMA0CNT_H:
		case MEM_ARM7_REG_DMA0CNT_H + 1:
		case MEM_ARM7_REG_DMA1SAD:
		case MEM_ARM7_REG_DMA1SAD + 1:
		case MEM_ARM7_REG_DMA1SAD + 2:
		case MEM_ARM7_REG_DMA1SAD + 3:
		case MEM_ARM7_REG_DMA1DAD:
		case MEM_ARM7_REG_DMA1DAD + 1:
		case MEM_ARM7_REG_DMA1DAD + 2:
		case MEM_ARM7_REG_DMA1DAD + 3:
		case MEM_ARM7_REG_DMA1CNT_L:
		case MEM_ARM7_REG_DMA1CNT_L + 1:
		case MEM_ARM7_REG_DMA1CNT_H:
		case MEM_ARM7_REG_DMA1CNT_H + 1:
		case MEM_ARM7_REG_DMA2SAD:
		case MEM_ARM7_REG_DMA2SAD + 1:
		case MEM_ARM7_REG_DMA2SAD + 2:
		case MEM_ARM7_REG_DMA2SAD + 3:
		case MEM_ARM7_REG_DMA2DAD:
		case MEM_ARM7_REG_DMA2DAD + 1:
		case MEM_ARM7_REG_DMA2DAD + 2:
		case MEM_ARM7_REG_DMA2DAD + 3:
		case MEM_ARM7_REG_DMA2CNT_L:
		case MEM_ARM7_REG_DMA2CNT_L + 1:
		case MEM_ARM7_REG_DMA2CNT_H:
		case MEM_ARM7_REG_DMA2CNT_H + 1:
		case MEM_ARM7_REG_DMA3SAD:
		case MEM_ARM7_REG_DMA3SAD + 1:
		case MEM_ARM7_REG_DMA3SAD + 2:
		case MEM_ARM7_REG_DMA3SAD + 3:
		case MEM_ARM7_REG_DMA3DAD:
		case MEM_ARM7_REG_DMA3DAD + 1:
		case MEM_ARM7_REG_DMA3DAD + 2:
		case MEM_ARM7_REG_DMA3DAD + 3:
		case MEM_ARM7_REG_DMA3CNT_L:
		case MEM_ARM7_REG_DMA3CNT_L  +1:
		case MEM_ARM7_REG_DMA3CNT_H:
		case MEM_ARM7_REG_DMA3CNT_H + 1:
		case MEM_ARM7_REG_POWCNT2:
		case MEM_ARM7_REG_POWCNT2 + 1:
		case MEM_ARM7_REG_POWCNT2 + 2:
		case MEM_ARM7_REG_POWCNT2 + 3:
		case MEM_ARM7_REG_RCNT:
		case MEM_ARM7_REG_RCNT + 1:
		case MEM_ARM7_REG_SOUNDCNT:
		case MEM_ARM7_REG_SOUNDCNT + 1:
		case MEM_ARM7_REG_SOUNDCNT + 2:
		case MEM_ARM7_REG_SOUNDCNT + 3:
		case MEM_ARM7_REG_WIFIWAITCNT:
		case MEM_ARM7_REG_WIFIWAITCNT + 1:
		case MEM_ARM7_REG_SNDCAP0CNT:
		case MEM_ARM7_REG_SNDCAP0DAD:
		case MEM_ARM7_REG_SNDCAP0DAD + 1:
		case MEM_ARM7_REG_SNDCAP0DAD + 2:
		case MEM_ARM7_REG_SNDCAP0DAD + 3:
		case MEM_ARM7_REG_SNDCAP1CNT:
		case MEM_ARM7_REG_SNDCAP1DAD:
		case MEM_ARM7_REG_SNDCAP1DAD + 1:
		case MEM_ARM7_REG_SNDCAP1DAD + 2:
		case MEM_ARM7_REG_SNDCAP1DAD + 3:
		case MEM_ARM7_REG_SOUNDXCNT(0):
		case MEM_ARM7_REG_SOUNDXCNT(0) + 1:
		case MEM_ARM7_REG_SOUNDXCNT(0) + 2:
		case MEM_ARM7_REG_SOUNDXCNT(0) + 3:
		case MEM_ARM7_REG_SOUNDXCNT(1):
		case MEM_ARM7_REG_SOUNDXCNT(1) + 1:
		case MEM_ARM7_REG_SOUNDXCNT(1) + 2:
		case MEM_ARM7_REG_SOUNDXCNT(1) + 3:
		case MEM_ARM7_REG_SOUNDXCNT(2):
		case MEM_ARM7_REG_SOUNDXCNT(2) + 1:
		case MEM_ARM7_REG_SOUNDXCNT(2) + 2:
		case MEM_ARM7_REG_SOUNDXCNT(2) + 3:
		case MEM_ARM7_REG_SOUNDXCNT(3):
		case MEM_ARM7_REG_SOUNDXCNT(3) + 1:
		case MEM_ARM7_REG_SOUNDXCNT(3) + 2:
		case MEM_ARM7_REG_SOUNDXCNT(3) + 3:
		case MEM_ARM7_REG_SOUNDXCNT(4):
		case MEM_ARM7_REG_SOUNDXCNT(4) + 1:
		case MEM_ARM7_REG_SOUNDXCNT(4) + 2:
		case MEM_ARM7_REG_SOUNDXCNT(4) + 3:
		case MEM_ARM7_REG_SOUNDXCNT(5):
		case MEM_ARM7_REG_SOUNDXCNT(5) + 1:
		case MEM_ARM7_REG_SOUNDXCNT(5) + 2:
		case MEM_ARM7_REG_SOUNDXCNT(5) + 3:
		case MEM_ARM7_REG_SOUNDXCNT(6):
		case MEM_ARM7_REG_SOUNDXCNT(6) + 1:
		case MEM_ARM7_REG_SOUNDXCNT(6) + 2:
		case MEM_ARM7_REG_SOUNDXCNT(6) + 3:
		case MEM_ARM7_REG_SOUNDXCNT(7):
		case MEM_ARM7_REG_SOUNDXCNT(7) + 1:
		case MEM_ARM7_REG_SOUNDXCNT(7) + 2:
		case MEM_ARM7_REG_SOUNDXCNT(7) + 3:
		case MEM_ARM7_REG_SOUNDXCNT(8):
		case MEM_ARM7_REG_SOUNDXCNT(8) + 1:
		case MEM_ARM7_REG_SOUNDXCNT(8) + 2:
		case MEM_ARM7_REG_SOUNDXCNT(8) + 3:
		case MEM_ARM7_REG_SOUNDXCNT(9):
		case MEM_ARM7_REG_SOUNDXCNT(9) + 1:
		case MEM_ARM7_REG_SOUNDXCNT(9) + 2:
		case MEM_ARM7_REG_SOUNDXCNT(9) + 3:
		case MEM_ARM7_REG_SOUNDXCNT(10):
		case MEM_ARM7_REG_SOUNDXCNT(10) + 1:
		case MEM_ARM7_REG_SOUNDXCNT(10) + 2:
		case MEM_ARM7_REG_SOUNDXCNT(10) + 3:
		case MEM_ARM7_REG_SOUNDXCNT(11):
		case MEM_ARM7_REG_SOUNDXCNT(11) + 1:
		case MEM_ARM7_REG_SOUNDXCNT(11) + 2:
		case MEM_ARM7_REG_SOUNDXCNT(11) + 3:
		case MEM_ARM7_REG_SOUNDXCNT(12):
		case MEM_ARM7_REG_SOUNDXCNT(12) + 1:
		case MEM_ARM7_REG_SOUNDXCNT(12) + 2:
		case MEM_ARM7_REG_SOUNDXCNT(12) + 3:
		case MEM_ARM7_REG_SOUNDXCNT(13):
		case MEM_ARM7_REG_SOUNDXCNT(13) + 1:
		case MEM_ARM7_REG_SOUNDXCNT(13) + 2:
		case MEM_ARM7_REG_SOUNDXCNT(13) + 3:
		case MEM_ARM7_REG_SOUNDXCNT(14):
		case MEM_ARM7_REG_SOUNDXCNT(14) + 1:
		case MEM_ARM7_REG_SOUNDXCNT(14) + 2:
		case MEM_ARM7_REG_SOUNDXCNT(14) + 3:
		case MEM_ARM7_REG_SOUNDXCNT(15):
		case MEM_ARM7_REG_SOUNDXCNT(15) + 1:
		case MEM_ARM7_REG_SOUNDXCNT(15) + 2:
		case MEM_ARM7_REG_SOUNDXCNT(15) + 3:
			return mem->arm7_regs[addr];
		case MEM_ARM7_REG_SPICNT:
		case MEM_ARM7_REG_SPICNT + 1:
#if 0
			printf("[%08" PRIx32 "] SPICNT[%08" PRIx32 "] read 0x%02" PRIx8 "\n",
			       cpu_get_reg(mem->nds->arm7, CPU_REG_PC), addr, mem->arm7_regs[addr]);
#endif
			return mem->arm7_regs[addr];
		case MEM_ARM7_REG_ROMCTRL:
		case MEM_ARM7_REG_ROMCTRL + 1:
		case MEM_ARM7_REG_ROMCTRL + 2:
		case MEM_ARM7_REG_ROMCTRL + 3:
		case MEM_ARM7_REG_ROMCMD:
		case MEM_ARM7_REG_ROMCMD + 1:
		case MEM_ARM7_REG_ROMCMD + 2:
		case MEM_ARM7_REG_ROMCMD + 3:
		case MEM_ARM7_REG_ROMCMD + 4:
		case MEM_ARM7_REG_ROMCMD + 5:
		case MEM_ARM7_REG_ROMCMD + 6:
		case MEM_ARM7_REG_ROMCMD + 7:
		case MEM_ARM7_REG_EXMEMSTAT:
		case MEM_ARM7_REG_EXMEMSTAT + 1:
		case MEM_ARM7_REG_KEYCNT:
		case MEM_ARM7_REG_KEYCNT + 1:
		case MEM_ARM7_REG_DISPSTAT:
		case MEM_ARM7_REG_DISPSTAT + 1:
		case MEM_ARM7_REG_VCOUNT:
		case MEM_ARM7_REG_VCOUNT + 1:
			return mem->arm9_regs[addr];
		case MEM_ARM7_REG_AUXSPICNT:
		case MEM_ARM7_REG_AUXSPICNT + 1:
#if 1
			printf("[ARM7] [%08" PRIx32 "] AUXSPICNT[%08" PRIx32 "] read 0x%02" PRIx8 "\n",
			       cpu_get_reg(mem->nds->arm7, CPU_REG_PC), addr, mem->arm9_regs[addr]);
#endif
			return mem->arm9_regs[addr];
		case MEM_ARM7_REG_AUXSPIDATA:
			return mbc_spi_read(mem->mbc);
		case MEM_ARM7_REG_ROMDATA:
		case MEM_ARM7_REG_ROMDATA + 1:
		case MEM_ARM7_REG_ROMDATA + 2:
		case MEM_ARM7_REG_ROMDATA + 3:
			return mbc_read(mem->mbc);
		case MEM_ARM7_REG_TM0CNT_L:
			return mem->arm7_timers[0].v;
		case MEM_ARM7_REG_TM0CNT_L + 1:
			return mem->arm7_timers[0].v >> 8;
		case MEM_ARM7_REG_TM1CNT_L:
			return mem->arm7_timers[1].v;
		case MEM_ARM7_REG_TM1CNT_L + 1:
			return mem->arm7_timers[1].v >> 8;
		case MEM_ARM7_REG_TM2CNT_L:
			return mem->arm7_timers[2].v;
		case MEM_ARM7_REG_TM2CNT_L + 1:
			return mem->arm7_timers[2].v >> 8;
		case MEM_ARM7_REG_TM3CNT_L:
			return mem->arm7_timers[3].v;
		case MEM_ARM7_REG_TM3CNT_L + 1:
			return mem->arm7_timers[3].v >> 8;
		case MEM_ARM7_REG_SPIDATA:
			return spi_read(mem);
		case MEM_ARM7_REG_SPIDATA + 1:
		case MEM_ARM7_REG_RTC + 1:
		case MEM_ARM7_REG_RTC + 2:
		case MEM_ARM7_REG_RTC + 3:
		case MEM_ARM7_REG_SIODATA32:
		case MEM_ARM7_REG_SIODATA32 + 1:
		case MEM_ARM7_REG_SIODATA32 + 2:
		case MEM_ARM7_REG_SIODATA32 + 3:
		case MEM_ARM7_REG_SIOCNT:
		case MEM_ARM7_REG_SIOCNT + 1:
		case MEM_ARM7_REG_SIOCNT + 2:
		case MEM_ARM7_REG_SIOCNT + 3:
		case MEM_ARM7_REG_EXTKEYIN + 1:
			return 0;
		case MEM_ARM7_REG_WRAMSTAT:
			return mem->arm9_regs[MEM_ARM9_REG_WRAMCNT];
		case MEM_ARM7_REG_RTC:
			return rtc_read(mem);
		case MEM_ARM7_REG_KEYINPUT:
		{
			uint8_t v = 0;
			if (!(mem->nds->joypad & NDS_BUTTON_A))
				v |= (1 << 0);
			if (!(mem->nds->joypad & NDS_BUTTON_B))
				v |= (1 << 1);
			if (!(mem->nds->joypad & NDS_BUTTON_SELECT))
				v |= (1 << 2);
			if (!(mem->nds->joypad & NDS_BUTTON_START))
				v |= (1 << 3);
			if (!(mem->nds->joypad & NDS_BUTTON_RIGHT))
				v |= (1 << 4);
			if (!(mem->nds->joypad & NDS_BUTTON_LEFT))
				v |= (1 << 5);
			if (!(mem->nds->joypad & NDS_BUTTON_UP))
				v |= (1 << 6);
			if (!(mem->nds->joypad & NDS_BUTTON_DOWN))
				v |= (1 << 7);
			return v;
		}
		case MEM_ARM7_REG_KEYINPUT + 1:
		{
			uint8_t v = 0;
			if (!(mem->nds->joypad & NDS_BUTTON_R))
				v |= (1 << 0);
			if (!(mem->nds->joypad & NDS_BUTTON_L))
				v |= (1 << 1);
			return v;
		}
		case MEM_ARM7_REG_EXTKEYIN:
		{
			uint8_t v = 0;
			if (!(mem->nds->joypad & NDS_BUTTON_X))
				v |= (1 << 0);
			if (!(mem->nds->joypad & NDS_BUTTON_Y))
				v |= (1 << 1);
			v |= (1 << 2);
			v |= (1 << 3);
			v |= (1 << 4);
			v |= (1 << 5);
			if (!mem->nds->touch)
				v |= (1 << 6);
			return v;
		}
		case MEM_ARM7_REG_IPCFIFOCNT:
		{
			uint8_t v = mem->arm7_regs[addr] & (1 << 2);
			if (mem->arm9_fifo.len < 4)
				v |= (1 << 0);
			if (mem->arm9_fifo.len == 64)
				v |= (1 << 1);
#if 0
			printf("ARM7 FIFOCNT[0] read %02" PRIx8 "\n", v);
#endif
			return v;
		}
		case MEM_ARM7_REG_IPCFIFOCNT + 1:
		{
			uint8_t v = mem->arm7_regs[addr] & 0xC4;
			if (mem->arm7_fifo.len < 4)
				v |= (1 << 0);
			if (mem->arm7_fifo.len == 64)
				v |= (1 << 1);
#if 0
			printf("ARM7 FIFOCNT[1] read %02" PRIx8 "\n", v);
#endif
			return v;
		}
		case MEM_ARM7_REG_IPCFIFORECV:
		case MEM_ARM7_REG_IPCFIFORECV + 1:
		case MEM_ARM7_REG_IPCFIFORECV + 2:
		case MEM_ARM7_REG_IPCFIFORECV + 3:
		{
#if 0
			printf("ARM7 IPCFIFO read\n");
#endif
			if (!(mem->arm7_regs[MEM_ARM7_REG_IPCFIFOCNT + 1] & (1 << 7)))
				return mem->arm7_fifo.latch[addr - MEM_ARM7_REG_IPCFIFORECV];
			if (!mem->arm7_fifo.len)
			{
				mem->arm7_regs[MEM_ARM7_REG_IPCFIFOCNT + 1] |= (1 << 6);
				return mem->arm7_fifo.latch[addr - MEM_ARM7_REG_IPCFIFORECV];
			}
			uint8_t v = mem->arm7_fifo.data[mem->arm7_fifo.pos];
			mem->arm7_fifo.pos = (mem->arm7_fifo.pos + 1) % 64;
			mem->arm7_fifo.latch[addr - MEM_ARM7_REG_IPCFIFORECV] = v;
			mem->arm7_fifo.len--;
			if (!mem->arm7_fifo.len
			 && (mem->arm9_regs[MEM_ARM9_REG_IPCFIFOCNT] & (1 << 2)))
				mem_arm9_if(mem, 1 << 17);
			return v;
		}
		case MEM_ARM7_REG_VRAMSTAT:
		{
			uint8_t v = 0;
			if ((mem_arm9_get_reg8(mem, MEM_ARM9_REG_VRAMCNT_C) & 0x87) == 0x82)
				v |= (1 << 0);
			if ((mem_arm9_get_reg8(mem, MEM_ARM9_REG_VRAMCNT_D) & 0x87) == 0x82)
				v |= (1 << 1);
			return v;
		}
		default:
			printf("[%08" PRIx32 "] unknown ARM7 get register %08" PRIx32 "\n",
			       cpu_get_reg(mem->nds->arm7, CPU_REG_PC), addr);
			break;
	}
	return 0;
}

static uint16_t get_arm7_reg16(mem_t *mem, uint32_t addr)
{
	return (get_arm7_reg8(mem, addr + 0) << 0)
	     | (get_arm7_reg8(mem, addr + 1) << 8);
}

static uint32_t get_arm7_reg32(mem_t *mem, uint32_t addr)
{
	return (get_arm7_reg8(mem, addr + 0) << 0)
	     | (get_arm7_reg8(mem, addr + 1) << 8)
	     | (get_arm7_reg8(mem, addr + 2) << 16)
	     | (get_arm7_reg8(mem, addr + 3) << 24);
}

static void arm7_instr_delay(mem_t *mem, const uint8_t *table, enum mem_type type)
{
	mem->nds->arm7->instr_delay += table[type];
}

#define MEM_ARM7_GET(size) \
uint##size##_t mem_arm7_get##size(mem_t *mem, uint32_t addr, enum mem_type type) \
{ \
	if (addr >= 0x10000000) \
		goto end; \
	if (size == 16) \
		addr &= ~1; \
	if (size == 32) \
		addr &= ~3; \
	switch ((addr >> 24) & 0xF) \
	{ \
		case 0x0: /* ARM7 bios */ \
			if (addr < sizeof(mem->arm7_bios)) \
			{ \
				uint32_t biosprot = mem_arm7_get_reg32(mem, MEM_ARM7_REG_BIOSPROT); \
				if (addr < biosprot && cpu_get_reg(mem->nds->arm7, CPU_REG_PC) >= biosprot) \
					return (uint##size##_t)0xFFFFFFFF; \
				arm7_instr_delay(mem, arm7_wram_cycles_##size, type); \
				return *(uint##size##_t*)&mem->arm7_bios[addr]; \
			} \
			break; \
		case 0x2: /* main memory */ \
			arm7_instr_delay(mem, arm7_mram_cycles_##size, type); \
			return *(uint##size##_t*)&mem->mram[addr & 0x3FFFFF]; \
		case 0x3: /* wram */ \
			arm7_instr_delay(mem, arm7_wram_cycles_##size, type); \
			if (!mem->arm7_wram_mask || addr >= 0x3800000) \
				return *(uint##size##_t*)&mem->arm7_wram[addr & 0xFFFF]; \
			return *(uint##size##_t*)&mem->wram[mem->arm7_wram_base \
			     + (addr & mem->arm7_wram_mask)]; \
		case 0x4: /* io ports */ \
			arm7_instr_delay(mem, arm7_wram_cycles_##size, type); \
			return get_arm7_reg##size(mem, addr - 0x4000000); \
		case 0x6: /* vram */ \
			break; \
		case 0x8: /* GBA */ \
		case 0x9: \
		case 0xA: \
			return 0xFF; \
		default: \
			break; \
	} \
end: \
	printf("[%08" PRIx32 "] unknown ARM7 get" #size " addr: %08" PRIx32 "\n", \
	       cpu_get_reg(mem->nds->arm7, CPU_REG_PC), addr); \
	return 0; \
}

MEM_ARM7_GET(8);
MEM_ARM7_GET(16);
MEM_ARM7_GET(32);

#define MEM_ARM7_SET(size) \
void mem_arm7_set##size(mem_t *mem, uint32_t addr, uint##size##_t v, enum mem_type type) \
{ \
	if (addr >= 0x10000000) \
		goto end; \
	if (size == 16) \
		addr &= ~1; \
	if (size == 32) \
		addr &= ~3; \
	switch ((addr >> 24) & 0xF) \
	{ \
		case 0x0: /* ARM7 bios */ \
			arm7_instr_delay(mem, arm7_wram_cycles_##size, type); \
			break; \
		case 0x2: /* main memory */ \
			*(uint##size##_t*)&mem->mram[addr & 0x3FFFFF] = v; \
			arm7_instr_delay(mem, arm7_mram_cycles_##size, type); \
			return; \
		case 0x3: /* wram */ \
			if (!mem->arm7_wram_mask || addr >= 0x3800000) \
				*(uint##size##_t*)&mem->arm7_wram[addr & 0xFFFF] = v; \
			else \
				*(uint##size##_t*)&mem->wram[mem->arm7_wram_base \
				                          + (addr & mem->arm7_wram_mask)] = v; \
			arm7_instr_delay(mem, arm7_wram_cycles_##size, type); \
			return; \
		case 0x4: /* io ports */ \
			set_arm7_reg##size(mem, addr - 0x4000000, v); \
			arm7_instr_delay(mem, arm7_wram_cycles_##size, type); \
			return; \
		case 0x6: /* vram */ \
			break; \
		case 0x8: /* GBA */ \
		case 0x9: \
		case 0xA: \
			return; \
		default: \
			break; \
	} \
end:; \
	printf("[%08" PRIx32 "] unknown ARM7 set" #size " addr: %08" PRIx32 "\n", \
	       cpu_get_reg(mem->nds->arm7, CPU_REG_PC), addr); \
}

MEM_ARM7_SET(8);
MEM_ARM7_SET(16);
MEM_ARM7_SET(32);

static void run_div(mem_t *mem)
{
	mem->arm9_regs[MEM_ARM9_REG_DIVCNT + 1] &= ~(1 << 7);
	if (!mem_arm9_get_reg32(mem, MEM_ARM9_REG_DIV_DENOM + 0)
	 && !mem_arm9_get_reg32(mem, MEM_ARM9_REG_DIV_DENOM + 1))
	{
		mem->arm9_regs[MEM_ARM9_REG_DIVCNT + 1] |= (1 << 6);
		return;
	}
	mem->arm9_regs[MEM_ARM9_REG_DIVCNT + 1] &= ~(1 << 6);
	switch (mem->arm9_regs[MEM_ARM9_REG_DIVCNT] & 0x3)
	{
		case 0x0:
		{
			int32_t num = mem_arm9_get_reg32(mem, MEM_ARM9_REG_DIV_NUMER);
			int32_t den = mem_arm9_get_reg32(mem, MEM_ARM9_REG_DIV_DENOM);
			int32_t div;
			int32_t rem;
			if (den)
			{
				div = num / den;
				rem = num % den;
			}
			else if (num == INT32_MIN && den == -1)
			{
				div = INT32_MIN;
				rem = 0;
			}
			else
			{
				div = num > 0 ? -1 : 1;
				rem = num;
			}
#if 0
			printf("DIV0: %" PRId32 " / %" PRId32 " = %" PRId32 " / %" PRId32 "\n",
			       num, den, div, rem);
#endif
			mem_arm9_set_reg32(mem, MEM_ARM9_REG_DIV_RESULT, div);
			mem_arm9_set_reg32(mem, MEM_ARM9_REG_DIVREM_RESULT, rem);
			break;
		}
		case 0x1:
		case 0x3:
		{
			int64_t num = mem_arm9_get_reg64(mem, MEM_ARM9_REG_DIV_NUMER);
			int32_t den = mem_arm9_get_reg32(mem, MEM_ARM9_REG_DIV_DENOM);
			int64_t div;
			int32_t rem;
			if (den)
			{
				div = num / den;
				rem = num % den;
			}
			else if (num == INT64_MIN && den == -1)
			{
				div = INT64_MIN;
				rem = 0;
			}
			else
			{
				div = num > 0 ? -1 : 1;
				rem = num;
			}
#if 0
			printf("DIV1: %" PRId64 " / %" PRId32 " = %" PRId64 " / %" PRId32 "\n",
			       num, den, div, rem);
#endif
			mem_arm9_set_reg64(mem, MEM_ARM9_REG_DIV_RESULT, div);
			mem_arm9_set_reg32(mem, MEM_ARM9_REG_DIVREM_RESULT, rem);
			break;
		}
		case 0x2:
		{
			int64_t num = mem_arm9_get_reg64(mem, MEM_ARM9_REG_DIV_NUMER);
			int64_t den = mem_arm9_get_reg64(mem, MEM_ARM9_REG_DIV_DENOM);
			int64_t div;
			int64_t rem;
			if (den)
			{
				div = num / den;
				rem = num % den;
			}
			else if (num == INT64_MIN && den == -1)
			{
				div = INT64_MIN;
				rem = 0;
			}
			else
			{
				div = num > 0 ? -1 : 1;
				rem = num;
			}
#if 0
			printf("DIV2: %" PRId64 " / %" PRId64 " = %" PRId64 " / %" PRId64 "\n",
			       num, den, div, rem);
#endif
			mem_arm9_set_reg64(mem, MEM_ARM9_REG_DIV_RESULT, div);
			mem_arm9_set_reg64(mem, MEM_ARM9_REG_DIVREM_RESULT, rem);
			break;
		}
	}
}

static void set_arm9_reg8(mem_t *mem, uint32_t addr, uint8_t v)
{
	switch (addr)
	{
		case MEM_ARM9_REG_IPCSYNC:
			return;
		case MEM_ARM9_REG_IPCSYNC + 1:
			mem->arm9_regs[addr] = v & 0x47;
			if ((v & (1 << 5))
			 && (mem->arm7_regs[MEM_ARM7_REG_IPCSYNC + 1] & (1 << 6)))
				mem_arm7_if(mem, 1 << 16);
#if 0
			printf("ARM9 IPCSYNC write 0x%02" PRIx8 "\n", v);
#endif
			return;
		case MEM_ARM9_REG_IPCSYNC + 2:
		case MEM_ARM9_REG_IPCSYNC + 3:
			return;
		case MEM_ARM9_REG_IE:
		case MEM_ARM9_REG_IE + 1:
		case MEM_ARM9_REG_IE + 2:
		case MEM_ARM9_REG_IE + 3:
			mem->arm9_regs[addr] = v;
#if 0
			printf("[ARM9] IE 0x%08" PRIx32 "\n", mem_arm9_get_reg32(mem, MEM_ARM9_REG_IE));
#endif
			return;
		case MEM_ARM9_REG_IME:
		case MEM_ARM9_REG_IME + 1:
		case MEM_ARM9_REG_IME + 2:
		case MEM_ARM9_REG_IME + 3:
		case MEM_ARM9_REG_POSTFLG:
		case MEM_ARM9_REG_POSTFLG + 1:
		case MEM_ARM9_REG_POSTFLG + 2:
		case MEM_ARM9_REG_POSTFLG + 3:
		case MEM_ARM9_REG_ROMCTRL:
		case MEM_ARM9_REG_ROMCTRL + 1:
		case MEM_ARM9_REG_ROMCMD:
		case MEM_ARM9_REG_ROMCMD + 1:
		case MEM_ARM9_REG_ROMCMD + 2:
		case MEM_ARM9_REG_ROMCMD + 3:
		case MEM_ARM9_REG_ROMCMD + 4:
		case MEM_ARM9_REG_ROMCMD + 5:
		case MEM_ARM9_REG_ROMCMD + 6:
		case MEM_ARM9_REG_ROMCMD + 7:
		case MEM_ARM9_REG_TM0CNT_L:
		case MEM_ARM9_REG_TM0CNT_L + 1:
		case MEM_ARM9_REG_TM0CNT_H + 1:
		case MEM_ARM9_REG_TM1CNT_L:
		case MEM_ARM9_REG_TM1CNT_L + 1:
		case MEM_ARM9_REG_TM1CNT_H + 1:
		case MEM_ARM9_REG_TM2CNT_L:
		case MEM_ARM9_REG_TM2CNT_L + 1:
		case MEM_ARM9_REG_TM2CNT_H + 1:
		case MEM_ARM9_REG_TM3CNT_L:
		case MEM_ARM9_REG_TM3CNT_L + 1:
		case MEM_ARM9_REG_TM3CNT_H + 1:
		case MEM_ARM9_REG_EXMEMCNT:
		case MEM_ARM9_REG_EXMEMCNT + 1:
		case MEM_ARM9_REG_DISPSTAT:
		case MEM_ARM9_REG_DISPSTAT + 1:
		case MEM_ARM9_REG_DMA0SAD:
		case MEM_ARM9_REG_DMA0SAD + 1:
		case MEM_ARM9_REG_DMA0SAD + 2:
		case MEM_ARM9_REG_DMA0SAD + 3:
		case MEM_ARM9_REG_DMA0DAD:
		case MEM_ARM9_REG_DMA0DAD + 1:
		case MEM_ARM9_REG_DMA0DAD + 2:
		case MEM_ARM9_REG_DMA0DAD + 3:
		case MEM_ARM9_REG_DMA0CNT_L:
		case MEM_ARM9_REG_DMA0CNT_L + 1:
		case MEM_ARM9_REG_DMA0CNT_H:
		case MEM_ARM9_REG_DMA1SAD:
		case MEM_ARM9_REG_DMA1SAD + 1:
		case MEM_ARM9_REG_DMA1SAD + 2:
		case MEM_ARM9_REG_DMA1SAD + 3:
		case MEM_ARM9_REG_DMA1DAD:
		case MEM_ARM9_REG_DMA1DAD + 1:
		case MEM_ARM9_REG_DMA1DAD + 2:
		case MEM_ARM9_REG_DMA1DAD + 3:
		case MEM_ARM9_REG_DMA1CNT_L:
		case MEM_ARM9_REG_DMA1CNT_L + 1:
		case MEM_ARM9_REG_DMA1CNT_H:
		case MEM_ARM9_REG_DMA2SAD:
		case MEM_ARM9_REG_DMA2SAD + 1:
		case MEM_ARM9_REG_DMA2SAD + 2:
		case MEM_ARM9_REG_DMA2SAD + 3:
		case MEM_ARM9_REG_DMA2DAD:
		case MEM_ARM9_REG_DMA2DAD + 1:
		case MEM_ARM9_REG_DMA2DAD + 2:
		case MEM_ARM9_REG_DMA2DAD + 3:
		case MEM_ARM9_REG_DMA2CNT_L:
		case MEM_ARM9_REG_DMA2CNT_L + 1:
		case MEM_ARM9_REG_DMA2CNT_H:
		case MEM_ARM9_REG_DMA3SAD:
		case MEM_ARM9_REG_DMA3SAD + 1:
		case MEM_ARM9_REG_DMA3SAD + 2:
		case MEM_ARM9_REG_DMA3SAD + 3:
		case MEM_ARM9_REG_DMA3DAD:
		case MEM_ARM9_REG_DMA3DAD + 1:
		case MEM_ARM9_REG_DMA3DAD + 2:
		case MEM_ARM9_REG_DMA3DAD + 3:
		case MEM_ARM9_REG_DMA3CNT_L:
		case MEM_ARM9_REG_DMA3CNT_L  +1:
		case MEM_ARM9_REG_DMA3CNT_H:
		case MEM_ARM9_REG_DMA0FILL:
		case MEM_ARM9_REG_DMA0FILL + 1:
		case MEM_ARM9_REG_DMA0FILL + 2:
		case MEM_ARM9_REG_DMA0FILL + 3:
		case MEM_ARM9_REG_DMA1FILL:
		case MEM_ARM9_REG_DMA1FILL + 1:
		case MEM_ARM9_REG_DMA1FILL + 2:
		case MEM_ARM9_REG_DMA1FILL + 3:
		case MEM_ARM9_REG_DMA2FILL:
		case MEM_ARM9_REG_DMA2FILL + 1:
		case MEM_ARM9_REG_DMA2FILL + 2:
		case MEM_ARM9_REG_DMA2FILL + 3:
		case MEM_ARM9_REG_DMA3FILL:
		case MEM_ARM9_REG_DMA3FILL + 1:
		case MEM_ARM9_REG_DMA3FILL + 2:
		case MEM_ARM9_REG_DMA3FILL + 3:
		case MEM_ARM9_REG_POWCNT1:
		case MEM_ARM9_REG_POWCNT1 + 1:
		case MEM_ARM9_REG_POWCNT1 + 2:
		case MEM_ARM9_REG_POWCNT1 + 3:
		case MEM_ARM9_REG_DISPCNT:
		case MEM_ARM9_REG_DISPCNT + 1:
		case MEM_ARM9_REG_DISPCNT + 2:
		case MEM_ARM9_REG_DISPCNT + 3:
		case MEM_ARM9_REG_DISPCNT + 0x1000:
		case MEM_ARM9_REG_DISPCNT + 0x1000 + 1:
		case MEM_ARM9_REG_DISPCNT + 0x1000 + 2:
		case MEM_ARM9_REG_DISPCNT + 0x1000 + 3:
		case MEM_ARM9_REG_MASTER_BRIGHT:
		case MEM_ARM9_REG_MASTER_BRIGHT + 1:
		case MEM_ARM9_REG_MASTER_BRIGHT + 2:
		case MEM_ARM9_REG_MASTER_BRIGHT + 3:
		case MEM_ARM9_REG_MASTER_BRIGHT + 0x1000:
		case MEM_ARM9_REG_MASTER_BRIGHT + 0x1000 + 1:
		case MEM_ARM9_REG_MASTER_BRIGHT + 0x1000 + 2:
		case MEM_ARM9_REG_MASTER_BRIGHT + 0x1000 + 3:
		case MEM_ARM9_REG_BG0CNT:
		case MEM_ARM9_REG_BG0CNT + 1:
		case MEM_ARM9_REG_BG0CNT + 0x1000:
		case MEM_ARM9_REG_BG0CNT + 0x1000 + 1:
		case MEM_ARM9_REG_BG1CNT:
		case MEM_ARM9_REG_BG1CNT + 1:
		case MEM_ARM9_REG_BG1CNT + 0x1000:
		case MEM_ARM9_REG_BG1CNT + 0x1000 + 1:
		case MEM_ARM9_REG_BG2CNT:
		case MEM_ARM9_REG_BG2CNT + 1:
		case MEM_ARM9_REG_BG2CNT + 0x1000:
		case MEM_ARM9_REG_BG2CNT + 0x1000 + 1:
		case MEM_ARM9_REG_BG3CNT:
		case MEM_ARM9_REG_BG3CNT + 1:
		case MEM_ARM9_REG_BG3CNT + 0x1000:
		case MEM_ARM9_REG_BG3CNT + 0x1000 + 1:
		case MEM_ARM9_REG_BG0HOFS:
		case MEM_ARM9_REG_BG0HOFS + 1:
		case MEM_ARM9_REG_BG0HOFS + 0x1000:
		case MEM_ARM9_REG_BG0HOFS + 0x1000 + 1:
		case MEM_ARM9_REG_BG0VOFS:
		case MEM_ARM9_REG_BG0VOFS + 1:
		case MEM_ARM9_REG_BG0VOFS + 0x1000:
		case MEM_ARM9_REG_BG0VOFS + 0x1000 + 1:
		case MEM_ARM9_REG_BG1HOFS:
		case MEM_ARM9_REG_BG1HOFS + 1:
		case MEM_ARM9_REG_BG1HOFS + 0x1000:
		case MEM_ARM9_REG_BG1HOFS + 0x1000 + 1:
		case MEM_ARM9_REG_BG1VOFS:
		case MEM_ARM9_REG_BG1VOFS + 1:
		case MEM_ARM9_REG_BG1VOFS + 0x1000:
		case MEM_ARM9_REG_BG1VOFS + 0x1000 + 1:
		case MEM_ARM9_REG_BG2HOFS:
		case MEM_ARM9_REG_BG2HOFS + 1:
		case MEM_ARM9_REG_BG2HOFS + 0x1000:
		case MEM_ARM9_REG_BG2HOFS + 0x1000 + 1:
		case MEM_ARM9_REG_BG2VOFS:
		case MEM_ARM9_REG_BG2VOFS + 1:
		case MEM_ARM9_REG_BG2VOFS + 0x1000:
		case MEM_ARM9_REG_BG2VOFS + 0x1000 + 1:
		case MEM_ARM9_REG_BG3HOFS:
		case MEM_ARM9_REG_BG3HOFS + 1:
		case MEM_ARM9_REG_BG3HOFS + 0x1000:
		case MEM_ARM9_REG_BG3HOFS + 0x1000 + 1:
		case MEM_ARM9_REG_BG3VOFS:
		case MEM_ARM9_REG_BG3VOFS + 1:
		case MEM_ARM9_REG_BG3VOFS + 0x1000:
		case MEM_ARM9_REG_BG3VOFS + 0x1000 + 1:
		case MEM_ARM9_REG_BG2PA:
		case MEM_ARM9_REG_BG2PA + 1:
		case MEM_ARM9_REG_BG2PA + 0x1000:
		case MEM_ARM9_REG_BG2PA + 0x1000 + 1:
		case MEM_ARM9_REG_BG2PB:
		case MEM_ARM9_REG_BG2PB + 1:
		case MEM_ARM9_REG_BG2PB + 0x1000:
		case MEM_ARM9_REG_BG2PB + 0x1000 + 1:
		case MEM_ARM9_REG_BG2PC:
		case MEM_ARM9_REG_BG2PC + 1:
		case MEM_ARM9_REG_BG2PC + 0x1000:
		case MEM_ARM9_REG_BG2PC + 0x1000 + 1:
		case MEM_ARM9_REG_BG2PD:
		case MEM_ARM9_REG_BG2PD + 1:
		case MEM_ARM9_REG_BG2PD + 0x1000:
		case MEM_ARM9_REG_BG2PD + 0x1000 + 1:
		case MEM_ARM9_REG_BG2X:
		case MEM_ARM9_REG_BG2X + 1:
		case MEM_ARM9_REG_BG2X + 2:
		case MEM_ARM9_REG_BG2X + 3:
		case MEM_ARM9_REG_BG2X + 0x1000:
		case MEM_ARM9_REG_BG2X + 0x1000 + 1:
		case MEM_ARM9_REG_BG2X + 0x1000 + 2:
		case MEM_ARM9_REG_BG2X + 0x1000 + 3:
		case MEM_ARM9_REG_BG2Y:
		case MEM_ARM9_REG_BG2Y + 1:
		case MEM_ARM9_REG_BG2Y + 2:
		case MEM_ARM9_REG_BG2Y + 3:
		case MEM_ARM9_REG_BG2Y + 0x1000:
		case MEM_ARM9_REG_BG2Y + 0x1000 + 1:
		case MEM_ARM9_REG_BG2Y + 0x1000 + 2:
		case MEM_ARM9_REG_BG2Y + 0x1000 + 3:
		case MEM_ARM9_REG_BG3PA:
		case MEM_ARM9_REG_BG3PA + 1:
		case MEM_ARM9_REG_BG3PA + 0x1000:
		case MEM_ARM9_REG_BG3PA + 0x1000 + 1:
		case MEM_ARM9_REG_BG3PB:
		case MEM_ARM9_REG_BG3PB + 1:
		case MEM_ARM9_REG_BG3PB + 0x1000:
		case MEM_ARM9_REG_BG3PB + 0x1000 + 1:
		case MEM_ARM9_REG_BG3PC:
		case MEM_ARM9_REG_BG3PC + 1:
		case MEM_ARM9_REG_BG3PC + 0x1000:
		case MEM_ARM9_REG_BG3PC + 0x1000 + 1:
		case MEM_ARM9_REG_BG3PD:
		case MEM_ARM9_REG_BG3PD + 1:
		case MEM_ARM9_REG_BG3PD + 0x1000:
		case MEM_ARM9_REG_BG3PD + 0x1000 + 1:
		case MEM_ARM9_REG_BG3X:
		case MEM_ARM9_REG_BG3X + 1:
		case MEM_ARM9_REG_BG3X + 2:
		case MEM_ARM9_REG_BG3X + 3:
		case MEM_ARM9_REG_BG3X + 0x1000:
		case MEM_ARM9_REG_BG3X + 0x1000 + 1:
		case MEM_ARM9_REG_BG3X + 0x1000 + 2:
		case MEM_ARM9_REG_BG3X + 0x1000 + 3:
		case MEM_ARM9_REG_BG3Y:
		case MEM_ARM9_REG_BG3Y + 1:
		case MEM_ARM9_REG_BG3Y + 2:
		case MEM_ARM9_REG_BG3Y + 3:
		case MEM_ARM9_REG_BG3Y + 0x1000:
		case MEM_ARM9_REG_BG3Y + 0x1000 + 1:
		case MEM_ARM9_REG_BG3Y + 0x1000 + 2:
		case MEM_ARM9_REG_BG3Y + 0x1000 + 3:
		case MEM_ARM9_REG_WIN0H:
		case MEM_ARM9_REG_WIN0H + 1:
		case MEM_ARM9_REG_WIN0H + 0x1000:
		case MEM_ARM9_REG_WIN0H + 0x1000 + 1:
		case MEM_ARM9_REG_WIN1H:
		case MEM_ARM9_REG_WIN1H + 1:
		case MEM_ARM9_REG_WIN1H + 0x1000:
		case MEM_ARM9_REG_WIN1H + 0x1000 + 1:
		case MEM_ARM9_REG_WIN0V:
		case MEM_ARM9_REG_WIN0V + 1:
		case MEM_ARM9_REG_WIN0V + 0x1000:
		case MEM_ARM9_REG_WIN0V + 0x1000 + 1:
		case MEM_ARM9_REG_WIN1V:
		case MEM_ARM9_REG_WIN1V + 1:
		case MEM_ARM9_REG_WIN1V + 0x1000:
		case MEM_ARM9_REG_WIN1V + 0x1000 + 1:
		case MEM_ARM9_REG_WININ:
		case MEM_ARM9_REG_WININ + 1:
		case MEM_ARM9_REG_WININ + 0x1000:
		case MEM_ARM9_REG_WININ + 0x1000 + 1:
		case MEM_ARM9_REG_WINOUT:
		case MEM_ARM9_REG_WINOUT + 1:
		case MEM_ARM9_REG_WINOUT + 0x1000:
		case MEM_ARM9_REG_WINOUT + 0x1000 + 1:
		case MEM_ARM9_REG_MOSAIC:
		case MEM_ARM9_REG_MOSAIC + 1:
		case MEM_ARM9_REG_MOSAIC + 2:
		case MEM_ARM9_REG_MOSAIC + 3:
		case MEM_ARM9_REG_MOSAIC + 0x1000:
		case MEM_ARM9_REG_MOSAIC + 0x1000 + 1:
		case MEM_ARM9_REG_MOSAIC + 0x1000 + 2:
		case MEM_ARM9_REG_MOSAIC + 0x1000 + 3:
		case MEM_ARM9_REG_BLDCNT:
		case MEM_ARM9_REG_BLDCNT + 1:
		case MEM_ARM9_REG_BLDCNT + 0x1000:
		case MEM_ARM9_REG_BLDCNT + 0x1000 + 1:
		case MEM_ARM9_REG_BLDALPHA:
		case MEM_ARM9_REG_BLDALPHA + 1:
		case MEM_ARM9_REG_BLDALPHA + 0x1000:
		case MEM_ARM9_REG_BLDALPHA + 0x1000 + 1:
		case MEM_ARM9_REG_BLDY:
		case MEM_ARM9_REG_BLDY + 1:
		case MEM_ARM9_REG_BLDY + 2:
		case MEM_ARM9_REG_BLDY + 3:
		case MEM_ARM9_REG_BLDY + 0x1000:
		case MEM_ARM9_REG_BLDY + 0x1000 + 1:
		case MEM_ARM9_REG_BLDY + 0x1000 + 2:
		case MEM_ARM9_REG_BLDY + 0x1000 + 3:
			mem->arm9_regs[addr] = v;
			return;
		case MEM_ARM9_REG_AUXSPICNT:
#if 1
			printf("[ARM9] AUXSPICNT[%08" PRIx32 "] = %02" PRIx8 "\n",
			       addr, v);
#endif
			mem->arm9_regs[addr] = v & ~(1 << 7);
			return;
		case MEM_ARM9_REG_AUXSPICNT + 1:
#if 1
			printf("[ARM9] AUXSPICNT[%08" PRIx32 "] = %02" PRIx8 "\n",
			       addr, v);
#endif
			mem->arm9_regs[addr] = v;
			return;
		case MEM_ARM7_REG_AUXSPIDATA:
			mbc_spi_write(mem->mbc, v);
			return;
		case MEM_ARM9_REG_ROMCTRL + 2:
			mem->arm9_regs[addr] = (mem->arm9_regs[addr] & (1 << 7))
			                     | (v & ~(1 << 7));
			return;
		case MEM_ARM9_REG_ROMCTRL + 3:
			mem->arm9_regs[addr] = v;
			if (v & 0x80)
			{
#if 0
				printf("[ARM9] MBC cmd %02" PRIx8 "\n", v);
#endif
				mbc_cmd(mem->mbc);
			}
			return;
		case MEM_ARM9_REG_ROMDATA:
		case MEM_ARM9_REG_ROMDATA + 1:
		case MEM_ARM9_REG_ROMDATA + 2:
		case MEM_ARM9_REG_ROMDATA + 3:
#if 0
			printf("[ARM9] MBC write %02" PRIx8 "\n", v);
#endif
			mbc_write(mem->mbc, v);
			return;
		case MEM_ARM9_REG_IF:
		case MEM_ARM9_REG_IF + 1:
		case MEM_ARM9_REG_IF + 2:
		case MEM_ARM9_REG_IF + 3:
			mem->arm9_regs[addr] &= ~v;
			return;
		case MEM_ARM9_REG_TM0CNT_H:
			arm9_timer_control(mem, 0, v);
			return;
		case MEM_ARM9_REG_TM1CNT_H:
			arm9_timer_control(mem, 1, v);
			return;
		case MEM_ARM9_REG_TM2CNT_H:
			arm9_timer_control(mem, 2, v);
			return;
		case MEM_ARM9_REG_TM3CNT_H:
			arm9_timer_control(mem, 3, v);
			return;
		case MEM_ARM9_REG_WRAMCNT:
#if 0
			printf("WRAMCNT = %02" PRIx8 "\n", v);
#endif
			v &= 3;
			switch (v)
			{
				case 0:
					mem->arm7_wram_base = 0;
					mem->arm7_wram_mask = 0;
					mem->arm9_wram_base = 0;
					mem->arm9_wram_mask = 0x7FFF;
					break;
				case 1:
					mem->arm7_wram_base = 0x4000;
					mem->arm7_wram_mask = 0x3FFF;
					mem->arm9_wram_base = 0;
					mem->arm9_wram_mask = 0x3FFF;
					break;
				case 2:
					mem->arm7_wram_base = 0;
					mem->arm7_wram_mask = 0x3FFF;
					mem->arm9_wram_base = 0x4000;
					mem->arm9_wram_mask = 0x3FFF;
					break;
				case 3:
					mem->arm7_wram_base = 0;
					mem->arm7_wram_mask = 0x7FFF;
					mem->arm9_wram_base = 0;
					mem->arm9_wram_mask = 0;
					break;
			}
			mem->arm9_regs[addr] = v;
			return;
		case MEM_ARM9_REG_KEYCNT:
		case MEM_ARM9_REG_KEYCNT + 1:
			mem->arm9_regs[addr] = v;
			nds_test_keypad_int(mem->nds);
			return;
		case MEM_ARM9_REG_IPCFIFOCNT:
#if 0
			printf("ARM9 FIFOCNT[0] write %02" PRIx8 "\n", v);
#endif
			if (v & (1 << 3))
			{
#if 0
				printf("ARM7 FIFO clean\n");
#endif
				mem->arm7_fifo.len = 0;
				mem->arm7_fifo.latch[0] = 0;
				mem->arm7_fifo.latch[1] = 0;
				mem->arm7_fifo.latch[2] = 0;
				mem->arm7_fifo.latch[3] = 0;
			}
			mem->arm9_regs[addr] &= ~(1 << 2);
			mem->arm9_regs[addr] |= v & (1 << 2);
			return;
		case MEM_ARM9_REG_IPCFIFOCNT + 1:
#if 0
			printf("ARM9 FIFOCNT[1] write %02" PRIx8 "\n", v);
#endif
			mem->arm9_regs[addr] &= ~0x84;
			mem->arm9_regs[addr] |= v & 0x84;
			if (v & (1 << 6))
				mem->arm9_regs[addr] &= ~(1 << 6);
			return;
		case MEM_ARM9_REG_IPCFIFOSEND:
		case MEM_ARM9_REG_IPCFIFOSEND + 1:
		case MEM_ARM9_REG_IPCFIFOSEND + 2:
		case MEM_ARM9_REG_IPCFIFOSEND + 3:
			if (!(mem->arm7_regs[MEM_ARM7_REG_IPCFIFOCNT + 1] & (1 << 7)))
				return;
			if (mem->arm7_fifo.len == 64)
			{
#if 0
				printf("ARM7 FIFO full\n");
#endif
				mem->arm9_regs[MEM_ARM9_REG_IPCFIFOCNT + 1] |= (1 << 6);
				return;
			}
			mem->arm7_fifo.data[(mem->arm7_fifo.pos + mem->arm7_fifo.len) % 64] = v;
			mem->arm7_fifo.len++;
			if (mem->arm7_fifo.len == 4
			 && (mem->arm7_regs[MEM_ARM7_REG_IPCFIFOCNT + 1] & (1 << 2)))
				mem_arm7_if(mem, 1 << 18);
#if 0
			printf("ARM9 IPCFIFO write (now %" PRIu8 ")\n", mem->arm7_fifo.len);
#endif
			return;
		case MEM_ARM9_REG_DMA0CNT_H + 1:
			mem->arm9_regs[addr] = v;
			arm9_dma_control(mem, 0);
			return;
		case MEM_ARM9_REG_DMA1CNT_H + 1:
			mem->arm9_regs[addr] = v;
			arm9_dma_control(mem, 1);
			return;
		case MEM_ARM9_REG_DMA2CNT_H + 1:
			mem->arm9_regs[addr] = v;
			arm9_dma_control(mem, 2);
			return;
		case MEM_ARM9_REG_DMA3CNT_H + 1:
			mem->arm9_regs[addr] = v;
			arm9_dma_control(mem, 3);
			return;
		case MEM_ARM9_REG_DIVCNT:
		case MEM_ARM9_REG_DIVCNT + 1:
		case MEM_ARM9_REG_DIVCNT + 2:
		case MEM_ARM9_REG_DIVCNT + 3:
		case MEM_ARM9_REG_DIV_NUMER:
		case MEM_ARM9_REG_DIV_NUMER + 1:
		case MEM_ARM9_REG_DIV_NUMER + 2:
		case MEM_ARM9_REG_DIV_NUMER + 3:
		case MEM_ARM9_REG_DIV_NUMER + 4:
		case MEM_ARM9_REG_DIV_NUMER + 5:
		case MEM_ARM9_REG_DIV_NUMER + 6:
		case MEM_ARM9_REG_DIV_NUMER + 7:
		case MEM_ARM9_REG_DIV_DENOM:
		case MEM_ARM9_REG_DIV_DENOM + 1:
		case MEM_ARM9_REG_DIV_DENOM + 2:
		case MEM_ARM9_REG_DIV_DENOM + 3:
		case MEM_ARM9_REG_DIV_DENOM + 4:
		case MEM_ARM9_REG_DIV_DENOM + 5:
		case MEM_ARM9_REG_DIV_DENOM + 6:
		case MEM_ARM9_REG_DIV_DENOM + 7:
			mem->arm9_regs[addr] = v;
			run_div(mem);
			return;
		default:
			printf("[%08" PRIx32 "] unknown ARM9 set register %08" PRIx32 " = %02" PRIx8 "\n",
			       cpu_get_reg(mem->nds->arm9, CPU_REG_PC), addr, v);
			break;
	}
}

static void set_arm9_reg16(mem_t *mem, uint32_t addr, uint16_t v)
{
	set_arm9_reg8(mem, addr + 0, v >> 0);
	set_arm9_reg8(mem, addr + 1, v >> 8);
}

static void set_arm9_reg32(mem_t *mem, uint32_t addr, uint32_t v)
{
	set_arm9_reg8(mem, addr + 0, v >> 0);
	set_arm9_reg8(mem, addr + 1, v >> 8);
	set_arm9_reg8(mem, addr + 2, v >> 16);
	set_arm9_reg8(mem, addr + 3, v >> 24);
}

static uint8_t get_arm9_reg8(mem_t *mem, uint32_t addr)
{
	switch (addr)
	{
		case MEM_ARM9_REG_IPCSYNC:
			return mem->arm7_regs[MEM_ARM7_REG_IPCSYNC + 1] & 0x7;
		case MEM_ARM9_REG_IPCSYNC + 1:
		case MEM_ARM9_REG_IPCSYNC + 2:
		case MEM_ARM9_REG_IPCSYNC + 3:
		case MEM_ARM9_REG_IE:
		case MEM_ARM9_REG_IE + 1:
		case MEM_ARM9_REG_IE + 2:
		case MEM_ARM9_REG_IE + 3:
		case MEM_ARM9_REG_IF:
		case MEM_ARM9_REG_IF + 1:
		case MEM_ARM9_REG_IF + 2:
		case MEM_ARM9_REG_IF + 3:
		case MEM_ARM9_REG_IME:
		case MEM_ARM9_REG_IME + 1:
		case MEM_ARM9_REG_IME + 2:
		case MEM_ARM9_REG_IME + 3:
		case MEM_ARM9_REG_POSTFLG:
		case MEM_ARM9_REG_POSTFLG + 1:
		case MEM_ARM9_REG_POSTFLG + 2:
		case MEM_ARM9_REG_POSTFLG + 3:
		case MEM_ARM9_REG_ROMCTRL:
		case MEM_ARM9_REG_ROMCTRL + 1:
		case MEM_ARM9_REG_ROMCTRL + 2:
		case MEM_ARM9_REG_ROMCTRL + 3:
		case MEM_ARM9_REG_ROMCMD:
		case MEM_ARM9_REG_ROMCMD + 1:
		case MEM_ARM9_REG_ROMCMD + 2:
		case MEM_ARM9_REG_ROMCMD + 3:
		case MEM_ARM9_REG_ROMCMD + 4:
		case MEM_ARM9_REG_ROMCMD + 5:
		case MEM_ARM9_REG_ROMCMD + 6:
		case MEM_ARM9_REG_ROMCMD + 7:
		case MEM_ARM9_REG_WRAMCNT:
		case MEM_ARM9_REG_EXMEMCNT:
		case MEM_ARM9_REG_EXMEMCNT + 1:
		case MEM_ARM9_REG_KEYCNT:
		case MEM_ARM9_REG_KEYCNT + 1:
		case MEM_ARM9_REG_DISPSTAT:
		case MEM_ARM9_REG_DISPSTAT + 1:
		case MEM_ARM9_REG_VCOUNT:
		case MEM_ARM9_REG_VCOUNT + 1:
		case MEM_ARM9_REG_DMA0SAD:
		case MEM_ARM9_REG_DMA0SAD + 1:
		case MEM_ARM9_REG_DMA0SAD + 2:
		case MEM_ARM9_REG_DMA0SAD + 3:
		case MEM_ARM9_REG_DMA0DAD:
		case MEM_ARM9_REG_DMA0DAD + 1:
		case MEM_ARM9_REG_DMA0DAD + 2:
		case MEM_ARM9_REG_DMA0DAD + 3:
		case MEM_ARM9_REG_DMA0CNT_L:
		case MEM_ARM9_REG_DMA0CNT_L + 1:
		case MEM_ARM9_REG_DMA0CNT_H:
		case MEM_ARM9_REG_DMA0CNT_H + 1:
		case MEM_ARM9_REG_DMA1SAD:
		case MEM_ARM9_REG_DMA1SAD + 1:
		case MEM_ARM9_REG_DMA1SAD + 2:
		case MEM_ARM9_REG_DMA1SAD + 3:
		case MEM_ARM9_REG_DMA1DAD:
		case MEM_ARM9_REG_DMA1DAD + 1:
		case MEM_ARM9_REG_DMA1DAD + 2:
		case MEM_ARM9_REG_DMA1DAD + 3:
		case MEM_ARM9_REG_DMA1CNT_L:
		case MEM_ARM9_REG_DMA1CNT_L + 1:
		case MEM_ARM9_REG_DMA1CNT_H:
		case MEM_ARM9_REG_DMA1CNT_H + 1:
		case MEM_ARM9_REG_DMA2SAD:
		case MEM_ARM9_REG_DMA2SAD + 1:
		case MEM_ARM9_REG_DMA2SAD + 2:
		case MEM_ARM9_REG_DMA2SAD + 3:
		case MEM_ARM9_REG_DMA2DAD:
		case MEM_ARM9_REG_DMA2DAD + 1:
		case MEM_ARM9_REG_DMA2DAD + 2:
		case MEM_ARM9_REG_DMA2DAD + 3:
		case MEM_ARM9_REG_DMA2CNT_L:
		case MEM_ARM9_REG_DMA2CNT_L + 1:
		case MEM_ARM9_REG_DMA2CNT_H:
		case MEM_ARM9_REG_DMA2CNT_H + 1:
		case MEM_ARM9_REG_DMA3SAD:
		case MEM_ARM9_REG_DMA3SAD + 1:
		case MEM_ARM9_REG_DMA3SAD + 2:
		case MEM_ARM9_REG_DMA3SAD + 3:
		case MEM_ARM9_REG_DMA3DAD:
		case MEM_ARM9_REG_DMA3DAD + 1:
		case MEM_ARM9_REG_DMA3DAD + 2:
		case MEM_ARM9_REG_DMA3DAD + 3:
		case MEM_ARM9_REG_DMA3CNT_L:
		case MEM_ARM9_REG_DMA3CNT_L  +1:
		case MEM_ARM9_REG_DMA3CNT_H:
		case MEM_ARM9_REG_DMA3CNT_H + 1:
		case MEM_ARM9_REG_DMA0FILL:
		case MEM_ARM9_REG_DMA0FILL + 1:
		case MEM_ARM9_REG_DMA0FILL + 2:
		case MEM_ARM9_REG_DMA0FILL + 3:
		case MEM_ARM9_REG_DMA1FILL:
		case MEM_ARM9_REG_DMA1FILL + 1:
		case MEM_ARM9_REG_DMA1FILL + 2:
		case MEM_ARM9_REG_DMA1FILL + 3:
		case MEM_ARM9_REG_DMA2FILL:
		case MEM_ARM9_REG_DMA2FILL + 1:
		case MEM_ARM9_REG_DMA2FILL + 2:
		case MEM_ARM9_REG_DMA2FILL + 3:
		case MEM_ARM9_REG_DMA3FILL:
		case MEM_ARM9_REG_DMA3FILL + 1:
		case MEM_ARM9_REG_DMA3FILL + 2:
		case MEM_ARM9_REG_DMA3FILL + 3:
		case MEM_ARM9_REG_POWCNT1:
		case MEM_ARM9_REG_POWCNT1 + 1:
		case MEM_ARM9_REG_POWCNT1 + 2:
		case MEM_ARM9_REG_POWCNT1 + 3:
		case MEM_ARM9_REG_DIVCNT:
		case MEM_ARM9_REG_DIVCNT + 1:
		case MEM_ARM9_REG_DIVCNT + 2:
		case MEM_ARM9_REG_DIVCNT + 3:
		case MEM_ARM9_REG_DIV_NUMER:
		case MEM_ARM9_REG_DIV_NUMER + 1:
		case MEM_ARM9_REG_DIV_NUMER + 2:
		case MEM_ARM9_REG_DIV_NUMER + 3:
		case MEM_ARM9_REG_DIV_NUMER + 4:
		case MEM_ARM9_REG_DIV_NUMER + 5:
		case MEM_ARM9_REG_DIV_NUMER + 6:
		case MEM_ARM9_REG_DIV_NUMER + 7:
		case MEM_ARM9_REG_DIV_DENOM:
		case MEM_ARM9_REG_DIV_DENOM + 1:
		case MEM_ARM9_REG_DIV_DENOM + 2:
		case MEM_ARM9_REG_DIV_DENOM + 3:
		case MEM_ARM9_REG_DIV_DENOM + 4:
		case MEM_ARM9_REG_DIV_DENOM + 5:
		case MEM_ARM9_REG_DIV_DENOM + 6:
		case MEM_ARM9_REG_DIV_DENOM + 7:
		case MEM_ARM9_REG_DIV_RESULT:
		case MEM_ARM9_REG_DIV_RESULT + 1:
		case MEM_ARM9_REG_DIV_RESULT + 2:
		case MEM_ARM9_REG_DIV_RESULT + 3:
		case MEM_ARM9_REG_DIV_RESULT + 4:
		case MEM_ARM9_REG_DIV_RESULT + 5:
		case MEM_ARM9_REG_DIV_RESULT + 6:
		case MEM_ARM9_REG_DIV_RESULT + 7:
		case MEM_ARM9_REG_DIVREM_RESULT:
		case MEM_ARM9_REG_DIVREM_RESULT + 1:
		case MEM_ARM9_REG_DIVREM_RESULT + 2:
		case MEM_ARM9_REG_DIVREM_RESULT + 3:
		case MEM_ARM9_REG_DIVREM_RESULT + 4:
		case MEM_ARM9_REG_DIVREM_RESULT + 5:
		case MEM_ARM9_REG_DIVREM_RESULT + 6:
		case MEM_ARM9_REG_DIVREM_RESULT + 7:
		case MEM_ARM9_REG_DISPCNT:
		case MEM_ARM9_REG_DISPCNT + 1:
		case MEM_ARM9_REG_DISPCNT + 2:
		case MEM_ARM9_REG_DISPCNT + 3:
		case MEM_ARM9_REG_DISPCNT + 0x1000:
		case MEM_ARM9_REG_DISPCNT + 0x1000 + 1:
		case MEM_ARM9_REG_DISPCNT + 0x1000 + 2:
		case MEM_ARM9_REG_DISPCNT + 0x1000 + 3:
		case MEM_ARM9_REG_MASTER_BRIGHT:
		case MEM_ARM9_REG_MASTER_BRIGHT + 1:
		case MEM_ARM9_REG_MASTER_BRIGHT + 2:
		case MEM_ARM9_REG_MASTER_BRIGHT + 3:
		case MEM_ARM9_REG_MASTER_BRIGHT + 0x1000:
		case MEM_ARM9_REG_MASTER_BRIGHT + 0x1000 + 1:
		case MEM_ARM9_REG_MASTER_BRIGHT + 0x1000 + 2:
		case MEM_ARM9_REG_MASTER_BRIGHT + 0x1000 + 3:
		case MEM_ARM9_REG_BG0CNT:
		case MEM_ARM9_REG_BG0CNT + 1:
		case MEM_ARM9_REG_BG1CNT:
		case MEM_ARM9_REG_BG1CNT + 1:
		case MEM_ARM9_REG_BG2CNT:
		case MEM_ARM9_REG_BG2CNT + 1:
		case MEM_ARM9_REG_BG3CNT:
		case MEM_ARM9_REG_BG3CNT + 1:
		case MEM_ARM9_REG_WININ:
		case MEM_ARM9_REG_WININ + 1:
		case MEM_ARM9_REG_WINOUT:
		case MEM_ARM9_REG_WINOUT + 1:
		case MEM_ARM9_REG_BLDCNT:
		case MEM_ARM9_REG_BLDCNT + 1:
		case MEM_ARM9_REG_BLDALPHA:
		case MEM_ARM9_REG_BLDALPHA + 1:
		case MEM_ARM9_REG_BG0CNT + 0x1000:
		case MEM_ARM9_REG_BG0CNT + 0x1000 + 1:
		case MEM_ARM9_REG_BG1CNT + 0x1000:
		case MEM_ARM9_REG_BG1CNT + 0x1000 + 1:
		case MEM_ARM9_REG_BG2CNT + 0x1000:
		case MEM_ARM9_REG_BG2CNT + 0x1000 + 1:
		case MEM_ARM9_REG_BG3CNT + 0x1000:
		case MEM_ARM9_REG_BG3CNT + 0x1000 + 1:
		case MEM_ARM9_REG_WININ + 0x1000:
		case MEM_ARM9_REG_WININ + 0x1000 + 1:
		case MEM_ARM9_REG_WINOUT + 0x1000:
		case MEM_ARM9_REG_WINOUT + 0x1000 + 1:
		case MEM_ARM9_REG_BLDCNT + 0x1000:
		case MEM_ARM9_REG_BLDCNT + 0x1000 + 1:
		case MEM_ARM9_REG_BLDALPHA + 0x1000:
		case MEM_ARM9_REG_BLDALPHA + 0x1000 + 1:
			return mem->arm9_regs[addr];
		case MEM_ARM9_REG_AUXSPICNT:
		case MEM_ARM9_REG_AUXSPICNT + 1:
#if 1
			printf("[ARM9] [%08" PRIx32 "] AUXSPICNT[%08" PRIx32 "] read 0x%02" PRIx8 "\n",
			       cpu_get_reg(mem->nds->arm9, CPU_REG_PC), addr, mem->arm9_regs[addr]);
#endif
			return mem->arm9_regs[addr];
		case MEM_ARM9_REG_AUXSPIDATA:
			return mbc_spi_read(mem->mbc);
		case MEM_ARM9_REG_ROMDATA:
		case MEM_ARM9_REG_ROMDATA + 1:
		case MEM_ARM9_REG_ROMDATA + 2:
		case MEM_ARM9_REG_ROMDATA + 3:
			return mbc_read(mem->mbc);
		case MEM_ARM9_REG_TM0CNT_L:
			return mem->arm9_timers[0].v;
		case MEM_ARM9_REG_TM0CNT_L + 1:
			return mem->arm9_timers[0].v >> 8;
		case MEM_ARM9_REG_TM1CNT_L:
			return mem->arm9_timers[1].v;
		case MEM_ARM9_REG_TM1CNT_L + 1:
			return mem->arm9_timers[1].v >> 8;
		case MEM_ARM9_REG_TM2CNT_L:
			return mem->arm9_timers[2].v;
		case MEM_ARM9_REG_TM2CNT_L + 1:
			return mem->arm9_timers[2].v >> 8;
		case MEM_ARM9_REG_TM3CNT_L:
			return mem->arm9_timers[3].v;
		case MEM_ARM9_REG_TM3CNT_L + 1:
			return mem->arm9_timers[3].v >> 8;
		case MEM_ARM9_REG_KEYINPUT:
		{
			uint8_t v = 0;
			if (!(mem->nds->joypad & NDS_BUTTON_A))
				v |= (1 << 0);
			if (!(mem->nds->joypad & NDS_BUTTON_B))
				v |= (1 << 1);
			if (!(mem->nds->joypad & NDS_BUTTON_SELECT))
				v |= (1 << 2);
			if (!(mem->nds->joypad & NDS_BUTTON_START))
				v |= (1 << 3);
			if (!(mem->nds->joypad & NDS_BUTTON_RIGHT))
				v |= (1 << 4);
			if (!(mem->nds->joypad & NDS_BUTTON_LEFT))
				v |= (1 << 5);
			if (!(mem->nds->joypad & NDS_BUTTON_UP))
				v |= (1 << 6);
			if (!(mem->nds->joypad & NDS_BUTTON_DOWN))
				v |= (1 << 7);
			return v;
		}
		case MEM_ARM9_REG_KEYINPUT + 1:
		{
			uint8_t v = 0;
			if (!(mem->nds->joypad & NDS_BUTTON_R))
				v |= (1 << 0);
			if (!(mem->nds->joypad & NDS_BUTTON_L))
				v |= (1 << 1);
			return v;
		}
		case MEM_ARM9_REG_IPCFIFOCNT:
		{
			uint8_t v = mem->arm9_regs[addr] & (1 << 2);
			if (mem->arm7_fifo.len < 4)
				v |= (1 << 0);
			if (mem->arm7_fifo.len == 64)
				v |= (1 << 1);
#if 0
			printf("ARM9 FIFOCNT[0] read %02" PRIx8 "\n", v);
#endif
			return v;
		}
		case MEM_ARM9_REG_IPCFIFOCNT + 1:
		{
			uint8_t v = mem->arm9_regs[addr] & 0xC4;
			if (mem->arm9_fifo.len < 4)
				v |= (1 << 0);
			if (mem->arm9_fifo.len == 64)
				v |= (1 << 1);
#if 0
			printf("ARM9 FIFOCNT[1] read %02" PRIx8 "\n", v);
#endif
			return v;
		}
		case MEM_ARM9_REG_IPCFIFORECV:
		case MEM_ARM9_REG_IPCFIFORECV + 1:
		case MEM_ARM9_REG_IPCFIFORECV + 2:
		case MEM_ARM9_REG_IPCFIFORECV + 3:
		{
#if 0
			printf("ARM9 IPCFIFO read\n");
#endif
			if (!(mem->arm9_regs[MEM_ARM9_REG_IPCFIFOCNT + 1] & (1 << 7)))
				return mem->arm9_fifo.latch[addr - MEM_ARM9_REG_IPCFIFORECV];
			if (!mem->arm9_fifo.len)
			{
				mem->arm9_regs[MEM_ARM9_REG_IPCFIFOCNT + 1] |= (1 << 6);
				return mem->arm9_fifo.latch[addr - MEM_ARM9_REG_IPCFIFORECV];
			}
			uint8_t v = mem->arm9_fifo.data[mem->arm9_fifo.pos];
			mem->arm9_fifo.pos = (mem->arm9_fifo.pos + 1) % 64;
			mem->arm9_fifo.latch[addr - MEM_ARM9_REG_IPCFIFORECV] = v;
			mem->arm9_fifo.len--;
			if (!mem->arm9_fifo.len
			 && (mem->arm7_regs[MEM_ARM7_REG_IPCFIFOCNT] & (1 << 2)))
				mem_arm7_if(mem, 1 << 17);
			return v;
		}
		default:
			printf("[%08" PRIx32 "] unknown ARM9 get register %08" PRIx32 "\n",
			       cpu_get_reg(mem->nds->arm9, CPU_REG_PC), addr);
			break;
	}
	return 0;
}

static uint16_t get_arm9_reg16(mem_t *mem, uint32_t addr)
{
	return (get_arm9_reg8(mem, addr + 0) << 0)
	     | (get_arm9_reg8(mem, addr + 1) << 8);
}

static uint32_t get_arm9_reg32(mem_t *mem, uint32_t addr)
{
	return (get_arm9_reg8(mem, addr + 0) << 0)
	     | (get_arm9_reg8(mem, addr + 1) << 8)
	     | (get_arm9_reg8(mem, addr + 2) << 16)
	     | (get_arm9_reg8(mem, addr + 3) << 24);
}

static void arm9_instr_delay(mem_t *mem, const uint8_t *table, enum mem_type type)
{
	mem->nds->arm9->instr_delay += table[type];
}

static void *get_vram_bga_ptr(mem_t *mem, uint32_t addr)
{
	if (!mem->vram_bga_mask)
		return NULL;
	return &mem->vram[mem->vram_bga_base + (addr & mem->vram_bga_mask)];
}

static void *get_vram_bgb_ptr(mem_t *mem, uint32_t addr)
{
	if (!mem->vram_bgb_mask)
		return NULL;
	return &mem->vram[mem->vram_bgb_base + (addr & mem->vram_bgb_mask)];
}

static void *get_vram_obja_ptr(mem_t *mem, uint32_t addr)
{
	if (!mem->vram_obja_mask)
		return NULL;
	return &mem->vram[mem->vram_obja_base + (addr & mem->vram_obja_mask)];
}

static void *get_vram_objb_ptr(mem_t *mem, uint32_t addr)
{
	if (!mem->vram_objb_mask)
		return NULL;
	return &mem->vram[mem->vram_objb_base + (addr & mem->vram_objb_mask)];
}

static void *get_vram_ptr(mem_t *mem, uint32_t addr)
{
	switch ((addr >> 20) & 0xF)
	{
		case 0x0:
			return get_vram_bga_ptr(mem, addr);
		case 0x2:
			return get_vram_bgb_ptr(mem, addr);
		case 0x4:
			return get_vram_obja_ptr(mem, addr);
		case 0x6:
			return get_vram_objb_ptr(mem, addr);
		case 0x8:
			switch ((addr >> 16) & 0xF)
			{
				case 0x0:
				case 0x1:
					return &mem->vram[MEM_VRAM_A_BASE + (addr & MEM_VRAM_A_MASK)];
				case 0x2:
				case 0x3:
					return &mem->vram[MEM_VRAM_B_BASE + (addr & MEM_VRAM_B_MASK)];
				case 0x4:
				case 0x5:
					return &mem->vram[MEM_VRAM_C_BASE + (addr & MEM_VRAM_C_MASK)];
				case 0x6:
				case 0x7:
					return &mem->vram[MEM_VRAM_D_BASE + (addr & MEM_VRAM_D_MASK)];
				case 0x8:
					return &mem->vram[MEM_VRAM_E_BASE + (addr & MEM_VRAM_E_MASK)];
				case 0x9:
					switch ((addr >> 14) & 0x3)
					{
						case 0x0:
							return &mem->vram[MEM_VRAM_F_BASE + (addr & MEM_VRAM_F_MASK)];
						case 0x1:
							return &mem->vram[MEM_VRAM_G_BASE + (addr & MEM_VRAM_G_MASK)];
						case 0x2:
						case 0x3:
							return &mem->vram[MEM_VRAM_H_BASE + (addr & MEM_VRAM_H_MASK)];
					}
					break;
				case 0xA:
					return &mem->vram[MEM_VRAM_I_BASE + (addr & MEM_VRAM_I_MASK)];
			}
			break;
	}
	return NULL;
}

#define MEM_ARM9_GET(size) \
uint##size##_t mem_arm9_get##size(mem_t *mem, uint32_t addr, enum mem_type type) \
{ \
	/* printf("[%08" PRIx32 "] ARM9 get" #size " addr: %08" PRIx32 "\n", cpu_get_reg(mem->nds->arm9, CPU_REG_PC), addr); */ \
	if (size == 16) \
		addr &= ~1; \
	if (size == 32) \
		addr &= ~3; \
	if (addr != MEM_DIRECT) \
	{ \
		if (mem->nds->arm9->cp15.cr & (1 << 18)) \
		{ \
			uint32_t itcm_size = 0x200 << ((mem->nds->arm9->cp15.itcm & 0x3E) >> 1); \
			if (addr < itcm_size) \
			{ \
				uint32_t a = addr; \
				a &= itcm_size - 1; \
				a &= 0x7FFF; \
				arm9_instr_delay(mem, arm9_tcm_cycles_##size, type); \
				return *(uint##size##_t*)&mem->itcm[a]; \
			} \
		} \
		if (mem->nds->arm9->cp15.cr & (1 << 16)) \
		{ \
			uint32_t dtcm_base = mem->nds->arm9->cp15.dtcm & 0xFFFFF000; \
			uint32_t dtcm_size = 0x200 << ((mem->nds->arm9->cp15.dtcm & 0x3E) >> 1); \
			if (addr >= dtcm_base && addr < dtcm_base + dtcm_size) \
			{ \
				uint32_t a = addr - dtcm_base; \
				a &= dtcm_size - 1; \
				a &= 0x3FFF; \
				arm9_instr_delay(mem, arm9_tcm_cycles_##size, type); \
				return *(uint##size##_t*)&mem->dtcm[a]; \
			} \
		} \
	} \
	if (addr >= 0xFFFF0000) \
	{ \
		uint32_t a = addr - 0xFFFF0000; \
		a &= 0xFFF; \
		arm9_instr_delay(mem, arm9_wram_cycles_##size, type); \
		return *(uint##size##_t*)&mem->arm9_bios[a]; \
	} \
	if (addr >= 0x10000000) \
		goto end; \
	switch ((addr >> 24) & 0xFF) \
	{ \
		case 0x2: /* main memory */ \
			arm9_instr_delay(mem, arm9_mram_cycles_##size, type); \
			return *(uint##size##_t*)&mem->mram[addr & 0x3FFFFF]; \
		case 0x3: /* shared wram */ \
			arm9_instr_delay(mem, arm9_wram_cycles_##size, type); \
			if (!mem->arm9_wram_mask) \
				return 0; \
			return *(uint##size##_t*)&mem->wram[mem->arm9_wram_base \
			                                  + (addr & mem->arm9_wram_mask)]; \
		case 0x4: /* io ports */ \
			arm9_instr_delay(mem, arm9_wram_cycles_##size, type); \
			return get_arm9_reg##size(mem, addr - 0x4000000); \
		case 0x5: /* palette */ \
			arm9_instr_delay(mem, arm9_vram_cycles_##size, type); \
			return *(uint##size##_t*)&mem->palette[addr & 0x7FF]; \
		case 0x6: /* vram */ \
		{ \
			void *ptr = get_vram_ptr(mem, addr & 0xFFFFFF); \
			if (ptr) \
			{ \
				arm9_instr_delay(mem, arm9_vram_cycles_##size, type); \
				return *(uint##size##_t*)ptr; \
			} \
			return 0; \
		} \
		case 0x7: /* oam */ \
			arm9_instr_delay(mem, arm9_wram_cycles_##size, type); \
			return *(uint##size##_t*)&mem->oam[addr & 0x7FF]; \
		case 0x8: /* GBA */ \
		case 0x9: \
		case 0xA: \
			return 0xFF; \
		default: \
			break; \
	} \
end: \
	printf("[%08" PRIx32 "] unknown ARM9 get" #size " addr: %08" PRIx32 "\n", \
	       cpu_get_reg(mem->nds->arm9, CPU_REG_PC), addr); \
	return 0; \
} \
uint##size##_t mem_vram_bga_get##size(mem_t *mem, uint32_t addr) \
{ \
	void *ptr = get_vram_bga_ptr(mem, addr); \
	if (!ptr) \
		return 0; \
	return *(uint##size##_t*)ptr; \
} \
uint##size##_t mem_vram_bgb_get##size(mem_t *mem, uint32_t addr) \
{ \
	void *ptr = get_vram_bgb_ptr(mem, addr); \
	if (!ptr) \
		return 0; \
	return *(uint##size##_t*)ptr; \
} \
uint##size##_t mem_vram_obja_get##size(mem_t *mem, uint32_t addr) \
{ \
	void *ptr = get_vram_obja_ptr(mem, addr); \
	if (!ptr) \
		return 0; \
	return *(uint##size##_t*)ptr; \
} \
uint##size##_t mem_vram_objb_get##size(mem_t *mem, uint32_t addr) \
{ \
	void *ptr = get_vram_objb_ptr(mem, addr); \
	if (!ptr) \
		return 0; \
	return *(uint##size##_t*)ptr; \
}

MEM_ARM9_GET(8);
MEM_ARM9_GET(16);
MEM_ARM9_GET(32);

#define MEM_ARM9_SET(size) \
void mem_arm9_set##size(mem_t *mem, uint32_t addr, uint##size##_t v, enum mem_type type) \
{ \
	/* printf("[%08" PRIx32 "] ARM9 set" #size " addr: %08" PRIx32 "\n", cpu_get_reg(mem->nds->arm9, CPU_REG_PC), addr); */ \
	if (size == 16) \
		addr &= ~1; \
	if (size == 32) \
		addr &= ~3; \
	if (addr != MEM_DIRECT) \
	{ \
		if (mem->nds->arm9->cp15.cr & (1 << 18)) \
		{ \
			uint32_t itcm_size = 0x200 << ((mem->nds->arm9->cp15.itcm & 0x3E) >> 1); \
			if (addr < itcm_size) \
			{ \
				/* printf("[%08" PRIx32 "] ITCM[%08" PRIx32 "] = %x\n", cpu_get_reg(mem->nds->arm9, CPU_REG_PC), addr, v); */ \
				uint32_t a = addr; \
				a &= itcm_size - 1; \
				a &= 0x7FFF; \
				*(uint##size##_t*)&mem->itcm[a] = v; \
				arm9_instr_delay(mem, arm9_tcm_cycles_##size, type); \
				return; \
			} \
		} \
		if (mem->nds->arm9->cp15.cr & (1 << 16)) \
		{ \
			uint32_t dtcm_base = mem->nds->arm9->cp15.dtcm & 0xFFFFF000; \
			uint32_t dtcm_size = 0x200 << ((mem->nds->arm9->cp15.dtcm & 0x3E) >> 1); \
			if (addr >= dtcm_base && addr < dtcm_base + dtcm_size) \
			{ \
				/* printf("[%08" PRIx32 "] DTCM[%08" PRIx32 "] = %x\n", cpu_get_reg(mem->nds->arm9, CPU_REG_PC), addr, v); */ \
				uint32_t a = addr - dtcm_base; \
				a &= dtcm_size - 1; \
				a &= 0x3FFF; \
				*(uint##size##_t*)&mem->dtcm[a] = v; \
				arm9_instr_delay(mem, arm9_tcm_cycles_##size, type); \
				return; \
			} \
		} \
	} \
	if (addr >= 0x10000000) \
		goto end; \
	switch ((addr >> 24) & 0xF) \
	{ \
		case 0x2: /* main memory */ \
			*(uint##size##_t*)&mem->mram[addr & 0x3FFFFF] = v; \
			arm9_instr_delay(mem, arm9_mram_cycles_##size, type); \
			return; \
		case 0x3: /* shared wram */ \
			if (!mem->arm9_wram_mask) \
				return; \
			*(uint##size##_t*)&mem->wram[mem->arm9_wram_base \
			                           + (addr & mem->arm9_wram_mask)] = v; \
			arm9_instr_delay(mem, arm9_wram_cycles_##size, type); \
			return; \
		case 0x4: /* io ports */ \
			set_arm9_reg##size(mem, addr - 0x4000000, v); \
			arm9_instr_delay(mem, arm9_wram_cycles_##size, type); \
			return; \
		case 0x5: /* palette */ \
			/* printf("palette write [%08" PRIx32 "] = %x\n", addr, v); */ \
			*(uint##size##_t*)&mem->palette[addr & 0x7FF] = v; \
			arm9_instr_delay(mem, arm9_vram_cycles_##size, type); \
			return; \
		case 0x6: /* vram */ \
		{ \
			/* printf("vram write [%08" PRIx32 "] = %x\n", addr, v); */ \
			void *ptr = get_vram_ptr(mem, addr & 0xFFFFFF); \
			if (ptr) \
			{ \
				arm9_instr_delay(mem, arm9_vram_cycles_##size, type); \
				*(uint##size##_t*)ptr = v; \
			} \
			else \
			{ \
				break; \
			} \
			return; \
		} \
		case 0x7: /* oam */ \
			/* printf("oam write [%08" PRIx32 "] = %x\n", addr, v); */ \
			*(uint##size##_t*)&mem->oam[addr & 0x7FF] = v; \
			arm9_instr_delay(mem, arm9_wram_cycles_##size, type); \
			return; \
		case 0x8: /* GBA */ \
		case 0x9: \
		case 0xA: \
			return; \
		default: \
			break; \
	} \
end:; \
	printf("[%08" PRIx32 "] unknown ARM9 set" #size " addr: %08" PRIx32 "\n", \
	       cpu_get_reg(mem->nds->arm9, CPU_REG_PC), addr); \
}

MEM_ARM9_SET(8);
MEM_ARM9_SET(16);
MEM_ARM9_SET(32);
