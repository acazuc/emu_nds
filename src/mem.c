#include "mem.h"
#include "cpu.h"
#include "nds.h"

#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>

mem_t *mem_new(nds_t *nds, mbc_t *mbc)
{
	mem_t *mem = calloc(sizeof(*mem), 1);
	if (!mem)
		return NULL;

	mem->nds = nds;
	mem->mbc = mbc;
	return mem;
}

void mem_del(mem_t *mem)
{
	if (!mem)
		return;
	free(mem);
}

static void set_arm7_reg8(mem_t *mem, uint32_t addr, uint8_t v)
{
	switch (addr)
	{
		case MEM_ARM7_REG_IPCSYNC:
			return;
		case MEM_ARM7_REG_IPCSYNC + 1:
			mem->arm7_regs[addr] = v & 0x47;
			if ((v & (1 << 13))
			 && (mem->arm9_regs[MEM_ARM9_REG_IPCSYNC] & (1 << 14)))
				mem->arm9_regs[MEM_ARM9_REG_IF] |= (1 << 16);
			return;
		case MEM_ARM7_REG_IPCSYNC + 2:
		case MEM_ARM7_REG_IPCSYNC + 3:
			return;
		case MEM_ARM7_REG_IE:
		case MEM_ARM7_REG_IE + 1:
		case MEM_ARM7_REG_IE + 2:
		case MEM_ARM7_REG_IE + 3:
		case MEM_ARM7_REG_IF:
		case MEM_ARM7_REG_IF + 1:
		case MEM_ARM7_REG_IF + 2:
		case MEM_ARM7_REG_IF + 3:
		case MEM_ARM9_REG_IME:
		case MEM_ARM9_REG_IME + 1:
			mem->arm7_regs[addr] = v;
			return;
		default:
			printf("unknown ARM7 register %08" PRIx32 "\n", addr);
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
		case MEM_ARM9_REG_IME:
		case MEM_ARM9_REG_IME + 1:
			return mem->arm7_regs[addr];
		default:
			printf("unknown ARM7 register %08" PRIx32 "\n", addr);
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

#define MEM_ARM7_GET(size) \
uint##size##_t mem_arm7_get##size(mem_t *mem, uint32_t addr) \
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
				if (cpu_get_reg(mem->nds->arm7, CPU_REG_PC) < sizeof(mem->arm7_bios)) \
					return *(uint##size##_t*)&mem->arm7_bios[addr]; \
				return (uint##size##_t)0xFFFFFFFF; \
			} \
			break; \
		case 0x2: /* main memory */ \
			break; \
		case 0x3: /* wram */ \
			break; \
		case 0x4: /* io ports */ \
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
	printf("[%08" PRIx32 "] unknown ARM7 set" #size " addr: %08" PRIx32 "\n", \
	       cpu_get_reg(mem->nds->arm7, CPU_REG_PC), addr); \
	return 0; \
}

MEM_ARM7_GET(8);
MEM_ARM7_GET(16);
MEM_ARM7_GET(32);

#define MEM_ARM7_SET(size) \
void mem_arm7_set##size(mem_t *mem, uint32_t addr, uint##size##_t val) \
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
			break; \
		case 0x2: /* main memory */ \
			break; \
		case 0x3: /* wram */ \
			break; \
		case 0x4: /* io ports */ \
			set_arm7_reg##size(mem, addr - 0x4000000, val); \
			break; \
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
	printf("[%08" PRIx32 "] unknown ARM7 get" #size " addr: %08" PRIx32 "\n", \
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
				mem->arm7_regs[MEM_ARM7_REG_IF] |= (1 << 16);
			return;
		case MEM_ARM9_REG_IPCSYNC + 2:
		case MEM_ARM9_REG_IPCSYNC + 3:
			return;
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
			mem->arm9_regs[addr] = v;
			return;
		default:
			printf("unknown ARM9 register %08" PRIx32 "\n", addr);
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
			return mem->arm9_regs[addr];
		default:
			printf("unknown ARM9 register %08" PRIx32 "\n", addr);
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

#define MEM_ARM9_GET(size) \
uint##size##_t mem_arm9_get##size(mem_t *mem, uint32_t addr) \
{ \
	if (mem->nds->arm9->cp15.cr & (1 << 16)) \
	{ \
		/* XXX test dtcm */ \
	} \
	if (addr >= 0xFFFF0000) \
	{ \
		uint32_t a = addr - 0xFFFF0000; \
		a &= 0xFFF; \
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
			break; \
		case 0x3: /* shared wram */ \
			break; \
		case 0x4: /* io ports */ \
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
void mem_arm9_set##size(mem_t *mem, uint32_t addr, uint##size##_t val) \
{ \
	if (mem->nds->arm9->cp15.cr & (1 << 16)) \
	{ \
		/* XXX test dtcm */ \
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
			break; \
		case 0x3: /* shared wram */ \
			break; \
		case 0x4: /* io ports */ \
			set_arm9_reg##size(mem, addr - 0x4000000, val); \
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
