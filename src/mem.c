#include "mem.h"
#include "cpu.h"
#include "nds.h"
#include "mbc.h"

#include <inttypes.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

static const uint16_t timer_masks[4] = {0, 0x7E, 0x1FE, 0x7FE};

static const uint8_t arm7_mram_cycles_32[] = {0, 4, 20, 4, 18};
static const uint8_t arm7_mram_cycles_16[] = {0, 2, 18, 2, 16};
static const uint8_t arm7_mram_cycles_8[]  = {0, 2, 18, 2, 16};

static const uint8_t arm7_wram_cycles_32[] = {0, 2,  2, 2,  2};
static const uint8_t arm7_wram_cycles_16[] = {0, 2,  2, 2,  2};
static const uint8_t arm7_wram_cycles_8[]  = {0, 2,  2, 2,  2};

static const uint8_t arm7_vram_cycles_32[] = {0, 4,  2, 4,  4};
static const uint8_t arm7_vram_cycles_16[] = {0, 2,  2, 2,  2};
static const uint8_t arm7_vram_cycles_8[]  = {0, 2,  2, 2,  2};

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
	for (size_t i = 0; i < 4; ++i) \
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

static uint8_t powerman_read(mem_t *mem)
{
	/* XXX */
	(void)mem;
	return 0;
}

static uint8_t firmware_read(mem_t *mem)
{
	switch (mem->spi_firmware.cmd)
	{
		case 0x0:
			return 0;
		case 0x3:
#if 0
			printf("firmware read %02x\n", mem->spi_firmware.cmd_data.read.v);
#endif
			return mem->spi_firmware.cmd_data.read.v;
	}
	return 0;
}

static uint8_t touchscreen_read(mem_t *mem)
{
	/* XXX */
	(void)mem;
	return 0;
}

static void powerman_write(mem_t *mem, uint8_t v)
{
	/* XXX */
	(void)mem;
	(void)v;
}

static void firmware_write(mem_t *mem, uint8_t v)
{
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
#if 1
			printf("[%08x] firmware read: [%x] = %x\n",
			       cpu_get_reg(mem->nds->arm7, CPU_REG_PC),
			       mem->spi_firmware.cmd_data.read.addr,
			       mem->firmware[mem->spi_firmware.cmd_data.read.addr & 0x3FFFF]);
#endif
			mem->spi_firmware.cmd_data.read.v = mem->firmware[mem->spi_firmware.cmd_data.read.addr & 0x3FFFF];
			mem->spi_firmware.cmd_data.read.addr++;
			return;
	}
}

static void touchscreen_write(mem_t *mem, uint8_t v)
{
	/* XXX */
	(void)mem;
	(void)v;
}

static void powerman_reset(mem_t *mem)
{
	/* XXX */
	(void)mem;
}

static void firmware_reset(mem_t *mem)
{
	mem->spi_firmware.cmd = 0;
}

static void touchscreen_reset(mem_t *mem)
{
	/* XXX */
	(void)mem;
}

static uint8_t spi_read(mem_t *mem)
{
#if 0
	printf("SPI read\n");
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
	printf("SPI write %02" PRIx8 "\n", v);
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
}

static void set_arm7_reg8(mem_t *mem, uint32_t addr, uint8_t v)
{
#if 0
	printf("ARM7 register [%08" PRIx32 "] = %02x\n", addr, v);
#endif
	switch (addr)
	{
		case MEM_ARM7_REG_IPCSYNC:
			return;
		case MEM_ARM7_REG_IPCSYNC + 1:
			mem->arm7_regs[addr] = v & 0x47;
			if ((v & (1 << 13))
			 && (mem->arm9_regs[MEM_ARM9_REG_IPCSYNC] & (1 << 14)))
				mem_arm9_if(mem, 1 << 16);
			return;
		case MEM_ARM7_REG_IPCSYNC + 2:
		case MEM_ARM7_REG_IPCSYNC + 3:
			return;
		case MEM_ARM7_REG_IE:
		case MEM_ARM7_REG_IE + 1:
		case MEM_ARM7_REG_IE + 2:
		case MEM_ARM7_REG_IE + 3:
			mem->arm7_regs[addr] = v;
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
			mem->arm7_regs[addr] = v;
			return;
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
		case MEM_ARM7_REG_AUXSPICNT:
		case MEM_ARM7_REG_AUXSPICNT + 1:
			mem->arm9_regs[addr] = v;
			return;
		case MEM_ARM7_REG_ROMDATA:
		case MEM_ARM7_REG_ROMDATA + 1:
		case MEM_ARM7_REG_ROMDATA + 2:
		case MEM_ARM7_REG_ROMDATA + 3:
			mbc_write(mem->mbc, v);
			return;
		case MEM_ARM7_REG_ROMCTRL + 2:
			mem->arm9_regs[addr] = (mem->arm9_regs[addr] & (1 << 7))
			                     | (v & ~(1 << 7));
			return;
		case MEM_ARM7_REG_ROMCTRL + 3:
			mem->arm9_regs[addr] = v;
			if (v & 0x80)
				mbc_cmd(mem->mbc);
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
			mem->arm7_regs[addr] = v & ~(1 << 7);
			return;
		case MEM_ARM7_REG_SPICNT + 1:
			mem->arm7_regs[addr] = v;
			return;
		case MEM_ARM7_REG_SPIDATA:
			spi_write(mem, v);
			return;
		case MEM_ARM7_REG_SPIDATA + 1:
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
					printf("HALT %08x %08x\n", mem_arm7_get_reg32(mem, MEM_ARM7_REG_IF), mem_arm7_get_reg32(mem, MEM_ARM7_REG_IE));
					mem->nds->arm7->state = CPU_STATE_HALT;
					return;
				case 3:
					printf("STOP %08x %08x\n", mem_arm7_get_reg32(mem, MEM_ARM7_REG_IF), mem_arm7_get_reg32(mem, MEM_ARM7_REG_IE));
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
		case MEM_ARM7_REG_WRAMSTAT:
			return;
		default:
			printf("unknown ARM7 set register %08" PRIx32 " = %02x\n", addr, v);
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
		case MEM_ARM7_REG_SPICNT:
		case MEM_ARM7_REG_SPICNT + 1:
		case MEM_ARM7_REG_HALTCNT:
		case MEM_ARM7_REG_BIOSPROT:
		case MEM_ARM7_REG_BIOSPROT + 1:
		case MEM_ARM7_REG_BIOSPROT + 2:
		case MEM_ARM7_REG_BIOSPROT + 3:
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
		case MEM_ARM7_REG_AUXSPICNT:
		case MEM_ARM7_REG_AUXSPICNT + 1:
			return mem->arm9_regs[addr];
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
			return 0;
		case MEM_ARM7_REG_WRAMSTAT:
			return mem->arm9_regs[MEM_ARM9_REG_WRAMCNT];
		default:
			printf("unknown ARM7 get register %08" PRIx32 "\n", addr);
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
	mem->nds->arm7->instr_delay += table[type] + 1;
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
		case 0x8: /* GBA rom */ \
		case 0x9: \
			break; \
		case 0xA: /* GBA ram */ \
			break; \
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
	if (((addr >> 24) & 0xF) < 0x8) \
	{ \
		if (size == 16) \
			addr &= ~1; \
		if (size == 32) \
			addr &= ~3; \
	} \
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
		case 0x8: /* GBA rom */ \
		case 0x9: \
			break; \
		case 0xA: /* GBA ram */ \
			break; \
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

static void set_arm9_reg8(mem_t *mem, uint32_t addr, uint8_t v)
{
	switch (addr)
	{
		case MEM_ARM9_REG_IPCSYNC:
			return;
		case MEM_ARM9_REG_IPCSYNC + 1:
			mem->arm9_regs[addr] = v & 0x47;
			if ((v & (1 << 13))
			 && (mem->arm7_regs[MEM_ARM7_REG_IPCSYNC] & (1 << 14)))
				mem_arm7_if(mem, 1 << 16);
			return;
		case MEM_ARM9_REG_IPCSYNC + 2:
		case MEM_ARM9_REG_IPCSYNC + 3:
			return;
		case MEM_ARM9_REG_IE:
		case MEM_ARM9_REG_IE + 1:
		case MEM_ARM9_REG_IE + 2:
		case MEM_ARM9_REG_IE + 3:
		case MEM_ARM9_REG_IME:
		case MEM_ARM9_REG_IME + 1:
		case MEM_ARM9_REG_IME + 2:
		case MEM_ARM9_REG_IME + 3:
		case MEM_ARM9_REG_POSTFLG:
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
		case MEM_ARM9_REG_AUXSPICNT:
		case MEM_ARM9_REG_AUXSPICNT + 1:
			mem->arm9_regs[addr] = v;
			return;
		case MEM_ARM9_REG_ROMCTRL + 2:
			mem->arm9_regs[addr] = (mem->arm9_regs[addr] & (1 << 7))
			                     | (v & ~(1 << 7));
			return;
		case MEM_ARM9_REG_ROMCTRL + 3:
			mem->arm9_regs[addr] = v;
			if (v & 0x80)
				mbc_cmd(mem->mbc);
			return;
		case MEM_ARM9_REG_ROMDATA:
		case MEM_ARM9_REG_ROMDATA + 1:
		case MEM_ARM9_REG_ROMDATA + 2:
		case MEM_ARM9_REG_ROMDATA + 3:
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
		default:
			printf("unknown ARM9 set register %08" PRIx32 "\n", addr);
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
		case MEM_ARM9_REG_AUXSPICNT:
		case MEM_ARM9_REG_AUXSPICNT + 1:
		case MEM_ARM9_REG_WRAMCNT:
			return mem->arm9_regs[addr];
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
		default:
			printf("unknown ARM9 get register %08" PRIx32 "\n", addr);
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
	mem->nds->arm9->instr_delay += table[type] + 1;
}

#define MEM_ARM9_GET(size) \
uint##size##_t mem_arm9_get##size(mem_t *mem, uint32_t addr, enum mem_type type) \
{ \
	if (mem->nds->arm9->cp15.cr & (1 << 16)) \
	{ \
		uint32_t dtcm_base = mem->nds->arm9->cp15.dtcm & 0xFFFFF000; \
		uint32_t dtcm_size = 0x200 << ((mem->nds->arm9->cp15.dtcm & 0x3E) >> 1); \
		if (addr >= dtcm_base && addr < dtcm_base + dtcm_size) \
		{ \
			if (size == 16) \
				addr &= ~1; \
			if (size == 32) \
				addr &= ~3; \
			uint32_t a = addr - dtcm_base; \
			a &= dtcm_size - 1; \
			a &= 0x3FFF; \
			arm9_instr_delay(mem, arm9_tcm_cycles_##size, type); \
			return *(uint##size##_t*)&mem->dtcm[a]; \
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
	if (size == 16) \
		addr &= ~1; \
	if (size == 32) \
		addr &= ~3; \
	switch ((addr >> 24) & 0xFF) \
	{ \
		case 0x0: /* TCM */ \
		case 0x1: \
			if (mem->nds->arm9->cp15.cr & (1 << 18)) \
			{ \
				/* XXX test itcm */ \
			} \
			break; \
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
		case 0x5: /* palettes */ \
			break; \
		case 0x6: /* vram */ \
			break; \
		case 0x7: /* oam */ \
			break; \
		case 0x8: /* GBA rom */ \
		case 0x9: \
			break; \
		case 0xA: /* GBA ram */ \
			break; \
		default: \
			break; \
	} \
end: \
	printf("[%08" PRIx32 "] unknown ARM9 get" #size " addr: %08" PRIx32 "\n", \
	       cpu_get_reg(mem->nds->arm9, CPU_REG_PC), addr); \
	return 0; \
}

MEM_ARM9_GET(8);
MEM_ARM9_GET(16);
MEM_ARM9_GET(32);

#define MEM_ARM9_SET(size) \
void mem_arm9_set##size(mem_t *mem, uint32_t addr, uint##size##_t v, enum mem_type type) \
{ \
	if (mem->nds->arm9->cp15.cr & (1 << 16)) \
	{ \
		uint32_t dtcm_base = mem->nds->arm9->cp15.dtcm & 0xFFFFF000; \
		uint32_t dtcm_size = 0x200 << ((mem->nds->arm9->cp15.dtcm & 0x3E) >> 1); \
		if (addr >= dtcm_base && addr < dtcm_base + dtcm_size) \
		{ \
			if (size == 16) \
				addr &= ~1; \
			if (size == 32) \
				addr &= ~3; \
			uint32_t a = addr - dtcm_base; \
			a &= dtcm_size - 1; \
			a &= 0x3FFF; \
			*(uint##size##_t*)&mem->dtcm[a] = v; \
			arm9_instr_delay(mem, arm9_tcm_cycles_##size, type); \
			return; \
		} \
	} \
	if (addr >= 0x10000000) \
		goto end; \
	if (((addr >> 24) & 0xF) < 0x8) \
	{ \
		if (size == 16) \
			addr &= ~1; \
		if (size == 32) \
			addr &= ~3; \
	} \
	switch ((addr >> 24) & 0xF) \
	{ \
		case 0x0: /* TCM */ \
		case 0x1: \
			if (mem->nds->arm9->cp15.cr & (1 << 18)) \
			{ \
				/* XXX test itcm */ \
			} \
			break; \
		case 0x2: /* main memory */ \
			*(uint##size##_t*)&mem->mram[addr & 0x3FFFFF] = v; \
			arm9_instr_delay(mem, arm9_mram_cycles_##size, type); \
			return; \
		case 0x3: /* shared wram */ \
			if (!mem->arm7_wram_mask) \
				return; \
			*(uint##size##_t*)&mem->wram[mem->arm9_wram_base \
			                           + (addr & mem->arm9_wram_mask)] = v; \
			arm9_instr_delay(mem, arm9_wram_cycles_##size, type); \
			return; \
		case 0x4: /* io ports */ \
			set_arm9_reg##size(mem, addr - 0x4000000, v); \
			arm9_instr_delay(mem, arm9_wram_cycles_##size, type); \
			return; \
		case 0x5: /* palettes */ \
			break; \
		case 0x6: /* vram */ \
			break; \
		case 0x7: /* oam */ \
			break; \
		case 0x8: /* GBA rom */ \
		case 0x9: \
			break; \
		case 0xA: /* GBA ram */ \
			break; \
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
