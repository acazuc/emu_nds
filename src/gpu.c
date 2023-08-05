#include "gpu.h"
#include "mem.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>

#define TO8(v) (((uint32_t)(v) * 527 + 23) >> 6)

#define RGB5TO8(v, a) \
{ \
	TO8((v >> 0xA) & 0x1F), \
	TO8((v >> 0x5) & 0x1F), \
	TO8((v >> 0x0) & 0x1F), \
	a, \
}

#define SETRGB5(d, v, a) \
do \
{ \
	uint8_t *dst = d; \
	dst[0] = TO8((v >> 0xA) & 0x1F); \
	dst[1] = TO8((v >> 0x5) & 0x1F); \
	dst[2] = TO8((v >> 0x0) & 0x1F); \
	dst[3] = a; \
} while (0)

#define I12_FMT "%+6.4f"
#define I12_PRT(v) (v / (float)(1 << 12))

enum layer_type
{
	LAYER_NONE,
	LAYER_BD,
	LAYER_BG0,
	LAYER_BG1,
	LAYER_BG2,
	LAYER_BG3,
	LAYER_OBJ,
};

struct line_buff
{
	uint8_t bg0[256 * 4];
	uint8_t bg1[256 * 4];
	uint8_t bg2[256 * 4];
	uint8_t bg3[256 * 4];
	uint8_t obj[256 * 4];
};

struct gpu *gpu_new(struct mem *mem)
{
	struct gpu *gpu = calloc(sizeof(*gpu), 1);
	if (!gpu)
		return NULL;

	gpu->mem = mem;
	gpu->enga.reg_base = 0;
	gpu->enga.pal_base = 0;
	gpu->enga.oam_base = 0;
	gpu->enga.get_vram_bg8  = mem_vram_bga_get8;
	gpu->enga.get_vram_bg16 = mem_vram_bga_get16;
	gpu->enga.get_vram_bg32 = mem_vram_bga_get32;
	gpu->enga.get_vram_obj8  = mem_vram_obja_get8;
	gpu->enga.get_vram_obj16 = mem_vram_obja_get16;
	gpu->enga.get_vram_obj32 = mem_vram_obja_get32;
	gpu->enga.engb = 0;
	gpu->engb.reg_base = 0x1000;
	gpu->engb.pal_base = 0x400;
	gpu->engb.oam_base = 0x400;
	gpu->engb.get_vram_bg8  = mem_vram_bgb_get8;
	gpu->engb.get_vram_bg16 = mem_vram_bgb_get16;
	gpu->engb.get_vram_bg32 = mem_vram_bgb_get32;
	gpu->engb.get_vram_obj8  = mem_vram_objb_get8;
	gpu->engb.get_vram_obj16 = mem_vram_objb_get16;
	gpu->engb.get_vram_obj32 = mem_vram_objb_get32;
	gpu->engb.engb = 1;
	return gpu;
}

void gpu_del(struct gpu *gpu)
{
	if (!gpu)
		return;
	free(gpu);
}

static inline uint32_t eng_get_reg8(struct gpu *gpu, struct gpu_eng *eng, uint32_t reg)
{
	return mem_arm9_get_reg8(gpu->mem, reg + eng->reg_base);
}

static inline uint32_t eng_get_reg16(struct gpu *gpu, struct gpu_eng *eng, uint32_t reg)
{
	return mem_arm9_get_reg16(gpu->mem, reg + eng->reg_base);
}

static inline uint32_t eng_get_reg32(struct gpu *gpu, struct gpu_eng *eng, uint32_t reg)
{
	return mem_arm9_get_reg32(gpu->mem, reg + eng->reg_base);
}

static void draw_background_3d(struct gpu *gpu, struct gpu_eng *eng,
                               uint8_t y, uint8_t bg, uint8_t *data)
{
	//printf("unhandled 3d background\n");
}

static void draw_background_text(struct gpu *gpu, struct gpu_eng *eng,
                                 uint8_t y, uint8_t bg, uint8_t *data)
{
	static const uint32_t mapwidths[]  = {32 * 8, 64 * 8, 32 * 8, 64 * 8};
	static const uint32_t mapheights[] = {32 * 8, 32 * 8, 64 * 8, 64 * 8};
	uint32_t dispcnt = eng_get_reg32(gpu, eng, MEM_ARM9_REG_DISPCNT);
	uint16_t bgcnt = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BG0CNT + bg * 2);
	uint16_t bghofs = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BG0HOFS + bg * 4) & 0x1FF;
	uint16_t bgvofs = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BG0VOFS + bg * 4) & 0x1FF;
	uint32_t ext_pal_base = 0x2000 * bg;
	if (bg < 2)
		ext_pal_base += 0x4000 * ((bgcnt >> 13) & 0x1);
	uint8_t size = (bgcnt >> 14) & 0x3;
	uint32_t tilebase = ((bgcnt >> 2) & 0xF) * 0x4000;
	uint32_t mapbase = ((bgcnt >> 8) & 0x1F) * 0x800;
	if (!eng->engb)
	{
		tilebase += ((dispcnt >> 24) & 0x7) * 0x10000;
		mapbase += ((dispcnt >> 27) & 0x7) * 0x10000;
	}
	uint32_t mapw = mapwidths[size];
	uint32_t maph = mapheights[size];
	int32_t vy = y + bgvofs;
	vy %= maph;
	if (vy < 0)
		vy += maph;
	uint32_t mapy = vy / 8;
	uint32_t mapyoff;
	if (mapy >= 32)
	{
		mapy -= 32;
		mapyoff = 0x800;
		if (size == 3)
			mapyoff += 0x800;
	}
	else
	{
		mapyoff = 0;
	}
	for (int32_t x = 0; x < 256; ++x)
	{
		int32_t vx = x + bghofs;
		vx %= mapw;
		if (vx < 0)
			vx += mapw;
		uint32_t mapx = vx / 8;
		uint32_t tilex = vx % 8;
		uint32_t tiley = vy % 8;
		uint32_t mapoff = mapyoff;
		if (mapx >= 32)
		{
			mapx -= 32;
			mapoff += 0x800;
		}
		uint32_t mapaddr = mapbase + mapoff;
		mapaddr += (mapx + mapy * 32) * 2;
		uint16_t map = eng->get_vram_bg16(gpu->mem, mapaddr);
		uint16_t tileid = map & 0x3FF;
		if (map & (1 << 10))
			tilex = 7 - tilex;
		if (map & (1 << 11))
			tiley = 7 - tiley;
		uint8_t paladdr;
		uint32_t tileaddr = tilebase;
		uint16_t val;
		if (bgcnt & (1 << 7))
		{
			tileaddr += tileid * 0x40;
			tileaddr += tilex + tiley * 8;
			paladdr = eng->get_vram_bg8(gpu->mem, tileaddr);
			if (!paladdr)
				continue;
			if (dispcnt & (1 << 30))
			{
				uint32_t addr = ext_pal_base | ((map & 0xF000) >> 3) | (paladdr * 2);
				if (eng->engb)
					val = mem_vram_bgepb_get16(gpu->mem, addr);
				else
					val = mem_vram_bgepa_get16(gpu->mem, addr);
			}
			else
			{
				val = mem_get_bg_palette(gpu->mem, eng->pal_base | (paladdr * 2));
			}
		}
		else
		{
			tileaddr += tileid * 0x20;
			tileaddr += tilex / 2 + tiley * 4;
			paladdr = eng->get_vram_bg8(gpu->mem, tileaddr);
			if (tilex & 1)
				paladdr >>= 4;
			else
				paladdr &= 0xF;
			if (!paladdr)
				continue;
			paladdr |= ((map >> 8) & 0xF0);
			val = mem_get_bg_palette(gpu->mem, eng->pal_base + paladdr * 2);
		}
		SETRGB5(&data[x * 4], val, 0xFF);
	}
}

static void draw_background_affine(struct gpu *gpu, struct gpu_eng *eng,
                                   uint8_t y, uint8_t bg, uint8_t *data)
{
	(void)y;
	static const uint32_t mapsizes[]  = {16 * 8, 32 * 8, 64 * 8, 128 * 8};
	uint16_t bgcnt = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BG0CNT + bg * 2);
	uint8_t size = (bgcnt >> 14) & 0x3;
	uint32_t dispcnt = eng_get_reg32(gpu, eng, MEM_ARM9_REG_DISPCNT);
	uint32_t tilebase = ((bgcnt >> 2) & 0xF) * 0x4000;
	uint32_t mapbase = ((bgcnt >> 8) & 0x1F) * 0x800;
	if (!eng->engb)
	{
		tilebase += ((dispcnt >> 24) & 0x7) * 0x10000;
		mapbase += ((dispcnt >> 27) & 0x7) * 0x10000;
	}
	uint32_t mapsize = mapsizes[size];
	uint8_t overflow;
	if (bg >= 2)
		overflow = (bgcnt >> 13) & 0x1;
	else
		overflow = 0;
	int16_t pa = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BG2PA + 0x10 * (bg - 2));
	int16_t pc = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BG2PC + 0x10 * (bg - 2));
	int32_t bgx = bg == 2 ? eng->bg2x : eng->bg3x;
	int32_t bgy = bg == 2 ? eng->bg2y : eng->bg3y;
	for (int32_t x = 0; x < 256; ++x)
	{
		int32_t vx = bgx / 256;
		int32_t vy = bgy / 256;
		bgx += pa;
		bgy += pc;
		if (overflow)
		{
			vx %= mapsize;
			vy %= mapsize;
			if (vx < 0)
				vx += mapsize;
			if (vy < 0)
				vy += mapsize;
		}
		else
		{
			if (vx < 0 || (uint32_t)vx >= mapsize
			 || vy < 0 || (uint32_t)vy >= mapsize)
				continue;
		}
		uint32_t mapx = vx / 8;
		uint32_t mapy = vy / 8;
		uint32_t tilex = vx % 8;
		uint32_t tiley = vy % 8;
		uint32_t mapaddr = mapbase + mapx + mapy * (mapsize / 8);
		uint16_t tileid = eng->get_vram_bg8(gpu->mem, mapaddr);
		uint8_t paladdr;
		uint32_t tileaddr = tilebase + tileid * 0x40;
		tileaddr += tilex + tiley * 8;
		paladdr = eng->get_vram_bg8(gpu->mem, tileaddr);
		if (!paladdr)
			continue;
		uint16_t val = mem_get_bg_palette(gpu->mem, eng->pal_base + paladdr * 2);
		SETRGB5(&data[x * 4], val, 0xFF);
	}
}

static void draw_background_ext_direct(struct gpu *gpu, struct gpu_eng *eng,
                                       uint8_t y, uint8_t bg, uint8_t *data)
{
	(void)y;
	static const uint32_t mapwidths[]  = {128, 128, 512, 512};
	static const uint32_t mapheights[] = {128, 256, 256, 512};
	uint16_t bgcnt = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BG0CNT + bg * 2);
	int16_t pa = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BG2PA + 0x10 * (bg - 2));
	int16_t pc = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BG2PC + 0x10 * (bg - 2));
	int32_t bgx = bg == 2 ? eng->bg2x : eng->bg3x;
	int32_t bgy = bg == 2 ? eng->bg2y : eng->bg3y;
	uint32_t baseaddr = ((bgcnt >> 2) & 0xF) * 0x4000;
	uint32_t size = (bgcnt >> 14) & 0x3;
	uint32_t mapwidth = mapwidths[size];
	uint32_t mapheight = mapheights[size];
	uint8_t overflow;
	if (bg >= 2)
		overflow = (bgcnt >> 13) & 0x1;
	else
		overflow = 0;
	for (int32_t x = 0; x < 256; ++x)
	{
		int32_t vx = bgx / 256;
		int32_t vy = bgy / 256;
		bgx += pa;
		bgy += pc;
		if (overflow)
		{
			vx %= mapwidth;
			vy %= mapheight;
			if (vx < 0)
				vx += mapwidth;
			if (vy < 0)
				vy += mapheight;
		}
		else
		{
			if (vx < 0 || (uint32_t)vx >= mapwidth
			 || vy < 0 || (uint32_t)vy >= mapheight)
				continue;
		}
		uint32_t addr = baseaddr + 2 * (vx + mapwidth * vy);
		uint16_t val = eng->get_vram_bg16(gpu->mem, addr);
		if (!(val & (1 << 15)))
			continue;
		SETRGB5(&data[x * 4], val, 0xFF);
	}
}

static void draw_background_ext_paletted(struct gpu *gpu, struct gpu_eng *eng,
                                         uint8_t y, uint8_t bg, uint8_t *data)
{
	(void)y;
	static const uint32_t mapwidths[]  = {128, 128, 512, 512};
	static const uint32_t mapheights[] = {128, 256, 256, 512};
	uint16_t bgcnt = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BG0CNT + bg * 2);
	int16_t pa = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BG2PA + 0x10 * (bg - 2));
	int16_t pc = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BG2PC + 0x10 * (bg - 2));
	int32_t bgx = bg == 2 ? eng->bg2x : eng->bg3x;
	int32_t bgy = bg == 2 ? eng->bg2y : eng->bg3y;
	uint32_t baseaddr = ((bgcnt >> 2) & 0xF) * 0x4000;
	uint32_t size = (bgcnt >> 14) & 0x3;
	uint32_t mapwidth = mapwidths[size];
	uint32_t mapheight = mapheights[size];
	uint8_t overflow;
	if (bg >= 2)
		overflow = (bgcnt >> 13) & 0x1;
	else
		overflow = 0;
	for (int32_t x = 0; x < 256; ++x)
	{
		int32_t vx = bgx / 256;
		int32_t vy = bgy / 256;
		bgx += pa;
		bgy += pc;
		if (overflow)
		{
			vx %= mapwidth;
			vy %= mapheight;
			if (vx < 0)
				vx += mapwidth;
			if (vy < 0)
				vy += mapheight;
		}
		else
		{
			if (vx < 0 || (uint32_t)vx >= mapwidth
			 || vy < 0 || (uint32_t)vy >= mapheight)
				continue;
		}
		uint32_t addr = baseaddr + vx + mapwidth * vy;
		uint8_t val = eng->get_vram_bg8(gpu->mem, addr);
		if (!val)
			continue;
		uint16_t col = mem_get_bg_palette(gpu->mem, eng->pal_base + val * 2);
		SETRGB5(&data[x * 4], col, 0xFF);
	}
}

static void draw_background_ext_tiled(struct gpu *gpu, struct gpu_eng *eng,
                                      uint8_t y, uint8_t bg, uint8_t *data)
{
	(void)y;
	static const uint32_t mapsizes[]  = {16 * 8, 32 * 8, 64 * 8, 128 * 8};
	uint16_t bgcnt = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BG0CNT + bg * 2);
	uint8_t size = (bgcnt >> 14) & 0x3;
	uint32_t dispcnt = eng_get_reg32(gpu, eng, MEM_ARM9_REG_DISPCNT);
	uint32_t ext_pal_base = 0x2000 * bg;
	uint32_t tilebase = ((bgcnt >> 2) & 0xF) * 0x4000;
	uint32_t mapbase = ((bgcnt >> 8) & 0x1F) * 0x800;
	if (!eng->engb)
	{
		tilebase += ((dispcnt >> 24) & 0x7) * 0x10000;
		mapbase += ((dispcnt >> 27) & 0x7) * 0x10000;
	}
	uint32_t mapsize = mapsizes[size];
	uint8_t overflow;
	if (bg >= 2)
		overflow = (bgcnt >> 13) & 0x1;
	else
		overflow = 0;
	int16_t pa = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BG2PA + 0x10 * (bg - 2));
	int16_t pc = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BG2PC + 0x10 * (bg - 2));
	int32_t bgx = bg == 2 ? eng->bg2x : eng->bg3x;
	int32_t bgy = bg == 2 ? eng->bg2y : eng->bg3y;
	for (int32_t x = 0; x < 256; ++x)
	{
		int32_t vx = bgx / 256;
		int32_t vy = bgy / 256;
		bgx += pa;
		bgy += pc;
		if (overflow)
		{
			vx %= mapsize;
			vy %= mapsize;
			if (vx < 0)
				vx += mapsize;
			if (vy < 0)
				vy += mapsize;
		}
		else
		{
			if (vx < 0 || (uint32_t)vx >= mapsize
			 || vy < 0 || (uint32_t)vy >= mapsize)
				continue;
		}
		uint32_t mapx = vx / 8;
		uint32_t mapy = vy / 8;
		uint32_t tilex = vx % 8;
		uint32_t tiley = vy % 8;
		uint32_t mapoff = 0;
		if (mapy >= 32)
		{
			mapy -= 32;
			mapoff = 0x800;
			if (size == 3)
				mapoff += 0x800;
		}
		if (mapx >= 32)
		{
			mapx -= 32;
			mapoff += 0x800;
		}
		uint32_t mapaddr = mapbase + mapoff;
		mapaddr += (mapx + mapy * 32) * 2;
		uint16_t map = eng->get_vram_bg16(gpu->mem, mapaddr);
		uint16_t tileid = map & 0x3FF;
		if (map & (1 << 10))
			tilex = 7 - tilex;
		if (map & (1 << 11))
			tiley = 7 - tiley;
		uint8_t paladdr;
		uint32_t tileaddr = tilebase;
		uint16_t val;
		if (bgcnt & (1 << 7))
		{
			tileaddr += tileid * 0x40;
			tileaddr += tilex + tiley * 8;
			paladdr = eng->get_vram_bg8(gpu->mem, tileaddr);
			if (!paladdr)
				continue;
			if (dispcnt & (1 << 30))
			{
				uint32_t addr = ext_pal_base | ((map & 0xF000) >> 3) | (paladdr * 2);
				if (eng->engb)
					val = mem_vram_bgepb_get16(gpu->mem, addr);
				else
					val = mem_vram_bgepa_get16(gpu->mem, addr);
			}
			else
			{
				val = mem_get_bg_palette(gpu->mem, eng->pal_base | (paladdr * 2));
			}
		}
		else
		{
			tileaddr += tileid * 0x20;
			tileaddr += tilex / 2 + tiley * 4;
			paladdr = eng->get_vram_bg8(gpu->mem, tileaddr);
			if (tilex & 1)
				paladdr >>= 4;
			else
				paladdr &= 0xF;
			if (!paladdr)
				continue;
			paladdr |= ((map >> 8) & 0xF0);
			val = mem_get_bg_palette(gpu->mem, eng->pal_base + paladdr * 2);
		}
		SETRGB5(&data[x * 4], val, 0xFF);
	}
}

static void draw_background_extended(struct gpu *gpu, struct gpu_eng *eng,
                                     uint8_t y, uint8_t bg, uint8_t *data)
{
	uint16_t bgcnt = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BG0CNT + bg * 2);
	if (bgcnt & (1 << 7))
	{
		if (bgcnt & (1 << 2))
			draw_background_ext_direct(gpu, eng, y, bg, data);
		else
			draw_background_ext_paletted(gpu, eng, y, bg, data);
	}
	else
	{
		draw_background_ext_tiled(gpu, eng, y, bg, data);
	}
}

static void draw_background_large(struct gpu *gpu, struct gpu_eng *eng,
                                  uint8_t y, uint8_t bg, uint8_t *data)
{
	(void)y;
	static const uint32_t mapwidths[]  = {512 , 1024, 512 , 1024};
	static const uint32_t mapheights[] = {1024, 512 , 1024, 512};
	uint16_t bgcnt = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BG0CNT + bg * 2);
	int16_t pa = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BG2PA + 0x10 * (bg - 2));
	int16_t pc = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BG2PC + 0x10 * (bg - 2));
	int32_t bgx = bg == 2 ? eng->bg2x : eng->bg3x;
	int32_t bgy = bg == 2 ? eng->bg2y : eng->bg3y;
	uint32_t size = (bgcnt >> 14) & 0x3;
	uint32_t mapwidth = mapwidths[size];
	uint32_t mapheight = mapheights[size];
	uint8_t overflow;
	if (bg >= 2)
		overflow = (bgcnt >> 13) & 0x1;
	else
		overflow = 0;
	for (int32_t x = 0; x < 256; ++x)
	{
		int32_t vx = bgx / 256;
		int32_t vy = bgy / 256;
		bgx += pa;
		bgy += pc;
		if (overflow)
		{
			vx %= mapwidth;
			vy %= mapheight;
			if (vx < 0)
				vx += mapwidth;
			if (vy < 0)
				vy += mapheight;
		}
		else
		{
			if (vx < 0 || (uint32_t)vx >= mapwidth
			 || vy < 0 || (uint32_t)vy >= mapheight)
				continue;
		}
		uint32_t addr = vx + mapwidth * vy;
		uint8_t val = eng->get_vram_bg8(gpu->mem, addr);
		if (!val)
			continue;
		uint16_t col = mem_get_bg_palette(gpu->mem, eng->pal_base + val * 2);
		SETRGB5(&data[x * 4], col, 0xFF);
	}
}

static void draw_objects(struct gpu *gpu, struct gpu_eng *eng, uint8_t y, uint8_t *data)
{
	static const uint8_t widths[16] =
	{
		8 , 16, 32, 64,
		16, 32, 32, 64,
		8 , 8 , 16, 32,
		0 , 0 , 0 , 0 ,
	};
	static const uint8_t heights[16] =
	{
		8 , 16, 32, 64,
		8 , 8 , 16, 32,
		16, 32, 32, 64,
		0 , 0 , 0 , 0 ,
	};
	for (size_t i = 0; i < 256; ++i)
		data[i * 4 + 3] = 0xE;
	uint32_t dispcnt = eng_get_reg32(gpu, eng, MEM_ARM9_REG_DISPCNT);
	for (uint8_t i = 0; i < 128; ++i)
	{
		uint16_t attr0 = mem_get_oam16(gpu->mem, eng->oam_base + i * 8);
		if ((attr0 & 0x300) == 0x200) /* disable flag */
			continue;
		uint8_t mode = (attr0 >> 10) & 0x3;
		if (mode == 3)
		{
			printf("unhandled obj3\n");
			continue;
		}
		int16_t objy = attr0 & 0xFF;
		if (objy >= 192)
			objy -= 256;
		if (objy > y)
			continue;
		uint16_t attr1 = mem_get_oam16(gpu->mem, eng->oam_base + i * 8 + 2);
		int16_t objx = attr1 & 0x1FF;
		if (objx >= 256)
			objx -= 512;
		uint8_t shape = (attr0 >> 14) & 0x3;
		uint8_t size = (attr1 >> 14) & 0x3;
		uint8_t width = widths[size + shape * 4];
		uint8_t height = heights[size + shape * 4];
		uint8_t basewidth = width;
		uint8_t baseheight = height;
		uint8_t doublesize = (attr0 >> 9) & 0x1;
		if (doublesize)
		{
			width *= 2;
			height *= 2;
		}
		if (objx + width <= 0)
			continue;
		if (objy + height <= y)
			continue;
		uint8_t affine = (attr0 >> 8) & 0x1;
		int16_t pa;
		int16_t pb;
		int16_t pc;
		int16_t pd;
		if (affine)
		{
			uint16_t affineidx = ((attr1 >> 9) & 0x1F) * 0x20;
			pa = mem_get_oam16(gpu->mem, eng->oam_base + affineidx + 0x06);
			pb = mem_get_oam16(gpu->mem, eng->oam_base + affineidx + 0x0E);
			pc = mem_get_oam16(gpu->mem, eng->oam_base + affineidx + 0x16);
			pd = mem_get_oam16(gpu->mem, eng->oam_base + affineidx + 0x1E);
		}
		else
		{
			pa = 0x100;
			pb = 0;
			pc = 0;
			pd = 0x100;
		}
		uint16_t attr2 = mem_get_oam16(gpu->mem, eng->oam_base + i * 8 + 4);
		uint16_t tileid = attr2 & 0x3FF;
		uint8_t palette = (attr2 >> 12) & 0xF;
		uint8_t color_mode = (attr0 >> 13) & 0x1;
		uint8_t priority = (attr2 >> 10) & 0x3;
		int16_t centerx = width / 2;
		int16_t centery = height / 2;
		for (int16_t x = 0; x < width; ++x)
		{
			int16_t screenx = objx + x;
			if (screenx < 0 || screenx >= 256)
				continue;
			int16_t xpos = x;
			int16_t ypos = y - objy;
			int32_t texx;
			int32_t texy;
			if (affine)
			{
				int32_t dx = xpos - centerx;
				int32_t dy = ypos - centery;
				int32_t midx = centerx;
				int32_t midy = centery;
				int32_t maxx = width;
				int32_t maxy = height;
				if (doublesize)
				{
					midx /= 2;
					midy /= 2;
					maxx /= 2;
					maxy /= 2;
				}
				texx = ((pa * dx + pb * dy) / 256) + midx;
				texy = ((pc * dx + pd * dy) / 256) + midy;
				if (texx < 0 || texx >= maxx
				 || texy < 0 || texy >= maxy)
					continue;
			}
			else
			{
				texx = xpos;
				texy = ypos;
				if (attr1 & (1 << 12))
					texx = basewidth - 1 - texx;
				if (attr1 & (1 << 13))
					texy = baseheight - 1 - texy;
			}
			int16_t tilex = texx / 8;
			int16_t tilebx = texx % 8;
			int16_t tiley = texy / 8;
			int16_t tileby = texy % 8;
			uint16_t tilepos = tileid;
			if (dispcnt & (1 << 4))
			{
				tilepos <<= (dispcnt >> 20) & 0x3;
				tilepos += tilex;
				uint16_t tmp = tiley * basewidth / 4;
				if (!color_mode)
					tmp /= 2;
				tilepos += tmp;
			}
			else
			{
				tilepos += tilex + tiley * 32;
			}
			if (color_mode)
				tilepos += tilex;
			uint32_t tileoff = tileby * 0x8 + tilebx;
			if (!color_mode)
				tileoff /= 2;
			uint16_t tilev = eng->get_vram_obj8(gpu->mem, (tilepos * 0x20) | tileoff);
			if (!color_mode)
			{
				if (tilebx & 1)
					tilev >>= 4;
				else
					tilev &= 0xF;
			}
			if (!tilev)
				continue;
			uint16_t col;
			if (color_mode)
			{
				if (dispcnt & (1 << 31))
				{
					tilev |= palette * 0x100;
					if (eng->engb)
						col = mem_vram_objepb_get16(gpu->mem, tilev * 2);
					else
						col = mem_vram_objepa_get16(gpu->mem, tilev * 2);
				}
				else
				{
					col = mem_get_obj_palette(gpu->mem, eng->pal_base | (tilev * 2));
				}
			}
			else
			{
				col = mem_get_obj_palette(gpu->mem, eng->pal_base | (palette * 0x20) | (tilev * 2));
			}
			if (mode == 2)
			{
				if (col)
					data[screenx * 4 + 3] |= 0x40;
				continue;
			}
			if (priority >= ((data[screenx * 4 + 3] >> 1) & 0x7))
				continue;
			SETRGB5(&data[screenx * 4], col, 0x80 | (mode & 1) | (priority << 1) | (data[screenx * 4 + 3] & 0x40));
		}
	}
}

static const uint8_t *layer_data(struct line_buff *line, enum layer_type layer, const uint8_t *bd_color, uint32_t n, uint8_t mask)
{
	switch (layer)
	{
		case LAYER_NONE:
			return NULL;
		case LAYER_BD:
			if (mask & (1 << 5))
				return bd_color;
			return NULL;
		case LAYER_BG0:
			if (mask & (1 << 0))
				return &line->bg0[n];
			return NULL;
		case LAYER_BG1:
			if (mask & (1 << 1))
				return &line->bg1[n];
			return NULL;
		case LAYER_BG2:
			if (mask & (1 << 2))
				return &line->bg2[n];
			return NULL;
		case LAYER_BG3:
			if (mask & (1 << 3))
				return &line->bg3[n];
			return NULL;
		case LAYER_OBJ:
			if (mask & (1 << 4))
				return &line->obj[n];
			return NULL;
	}
	assert(!"unknown layer");
	return NULL;
}

static void calcwindow(struct gpu *gpu, struct gpu_eng *eng, struct line_buff *line, uint8_t x, uint8_t y, uint8_t *winflags)
{
	uint32_t dispcnt = eng_get_reg32(gpu, eng, MEM_ARM9_REG_DISPCNT);
	uint16_t winin = eng_get_reg16(gpu, eng, MEM_ARM9_REG_WININ);
	uint16_t winout = eng_get_reg16(gpu, eng, MEM_ARM9_REG_WINOUT);
	if (dispcnt & (1 << 13))
	{
		uint8_t win0l = eng_get_reg16(gpu, eng, MEM_ARM9_REG_WIN0H + 1);
		uint8_t win0r = eng_get_reg16(gpu, eng, MEM_ARM9_REG_WIN0H);
		uint8_t win0t = eng_get_reg16(gpu, eng, MEM_ARM9_REG_WIN0V + 1);
		uint8_t win0b = eng_get_reg16(gpu, eng, MEM_ARM9_REG_WIN0V);
		if (win0l > win0r)
		{
			if (win0t > win0b)
			{
				if ((x < win0r || x >= win0l)
				 && (y >= win0t || y < win0b))
				{
					*winflags = winin & 0xFF;
					return;
				}
			}
			else
			{
				if ((x < win0r || x >= win0l)
				 && y >= win0t && y < win0b)
				{
					*winflags = winin & 0xFF;
					return;
				}
			}
		}
		else
		{
			if (win0t > win0b)
			{
				if (x >= win0l && x < win0r
				 && (y >= win0t || y < win0b))
				{
					*winflags = winin & 0xFF;
					return;
				}
			}
			else
			{
				if (x >= win0l && x < win0r
				 && y >= win0t && y < win0b)
				{
					*winflags = winin & 0xFF;
					return;
				}
			}
		}
	}
	if (dispcnt & (1 << 14))
	{
		uint8_t win1l = eng_get_reg16(gpu, eng, MEM_ARM9_REG_WIN1H + 1);
		uint8_t win1r = eng_get_reg16(gpu, eng, MEM_ARM9_REG_WIN1H);
		uint8_t win1t = eng_get_reg16(gpu, eng, MEM_ARM9_REG_WIN1V + 1);
		uint8_t win1b = eng_get_reg16(gpu, eng, MEM_ARM9_REG_WIN1V);
		if (win1l > win1r)
		{
			if (win1t > win1b)
			{
				if ((x < win1r || x >= win1l)
				 && (y >= win1t || y < win1b))
				{
					*winflags = winin >> 8;
					return;
				}
			}
			else
			{
				if ((x < win1r || x >= win1l)
				 && y >= win1t && y < win1b)
				{
					*winflags = winin >> 8;
					return;
				}
			}
		}
		else
		{
			if (win1t > win1b)
			{
				if (x >= win1l && x < win1r
				 && (y >= win1t || y < win1b))
				{
					*winflags = winin >> 8;
					return;
				}
			}
			else
			{
				if (x >= win1l && x < win1r
				 && y >= win1t && y < win1b)
				{
					*winflags = winin >> 8;
					return;
				}
			}
		}
	}
	if (dispcnt & (1 << 15))
	{
		if (line->obj[x * 4 + 3] & 0x40)
		{
			*winflags = winout >> 8;
			return;
		}
	}
	*winflags = winout & 0xFF;
}

static void compose(struct gpu *gpu, struct gpu_eng *eng, struct line_buff *line, uint8_t y)
{
	uint16_t bd_col = mem_get_bg_palette(gpu->mem, eng->pal_base + 0);
	uint8_t bd_color[4] = RGB5TO8(bd_col, 0xFF);
	uint8_t *dst = &eng->data[y * eng->pitch];
	for (size_t x = 0; x < 256; ++x, dst += 4)
	{
		*(uint32_t*)dst = *(uint32_t*)bd_color;
#if 0
		line->bg0[x * 4 + 0] = line->bg0[x * 4 + 0] / 4 + 0xBF;
		line->bg0[x * 4 + 1] = line->bg0[x * 4 + 1] / 4;
		line->bg0[x * 4 + 2] = line->bg0[x * 4 + 2] / 4;
		line->bg1[x * 4 + 0] = line->bg1[x * 4 + 0] / 4;
		line->bg1[x * 4 + 1] = line->bg1[x * 4 + 1] / 4 + 0xBF;
		line->bg1[x * 4 + 2] = line->bg1[x * 4 + 2] / 4;
		line->bg2[x * 4 + 0] = line->bg2[x * 4 + 0] / 4;
		line->bg2[x * 4 + 1] = line->bg2[x * 4 + 1] / 4;
		line->bg2[x * 4 + 2] = line->bg2[x * 4 + 2] / 4 + 0xBF;
		line->bg3[x * 4 + 0] = line->bg3[x * 4 + 0] / 4 + 0xBF;
		line->bg3[x * 4 + 1] = line->bg3[x * 4 + 1] / 4 + 0xBF;
		line->bg3[x * 4 + 2] = line->bg3[x * 4 + 2] / 4;
		line->obj[x * 4 + 0] = line->obj[x * 4 + 0] / 4 + 0xBF;
		line->obj[x * 4 + 1] = line->obj[x * 4 + 1] / 4;
		line->obj[x * 4 + 2] = line->obj[x * 4 + 2] / 4 + 0xBF;
#endif
#if 0
		uint8_t prio;
		prio = mem_arm9_get_reg16(gpu->mem, MEM_ARM9_REG_BG0CNT + eng->regoff) & 3;
		line->bg0[x * 4 + 0] = line->bg0[x * 4 + 0] / 8 + prio * 0x3F;
		line->bg0[x * 4 + 1] = line->bg0[x * 4 + 1] / 8 + prio * 0x3F;
		line->bg0[x * 4 + 2] = line->bg0[x * 4 + 2] / 8 + prio * 0x3F;
		prio = mem_arm9_get_reg16(gpu->mem, MEM_ARM9_REG_BG1CNT + eng->regoff) & 3;
		line->bg1[x * 4 + 0] = line->bg1[x * 4 + 0] / 8 + prio * 0x3F;
		line->bg1[x * 4 + 1] = line->bg1[x * 4 + 1] / 8 + prio * 0x3F;
		line->bg1[x * 4 + 2] = line->bg1[x * 4 + 2] / 8 + prio * 0x3F;
		prio = mem_arm9_get_reg16(gpu->mem, MEM_ARM9_REG_BG2CNT + eng->regoff) & 3;
		line->bg2[x * 4 + 0] = line->bg2[x * 4 + 0] / 8 + prio * 0x3F;
		line->bg2[x * 4 + 1] = line->bg2[x * 4 + 1] / 8 + prio * 0x3F;
		line->bg2[x * 4 + 2] = line->bg2[x * 4 + 2] / 8 + prio * 0x3F;
		prio = mem_arm9_get_reg16(gpu->mem, MEM_ARM9_REG_BG3CNT + eng->regoff) & 3;
		line->bg3[x * 4 + 0] = line->bg3[x * 4 + 0] / 8 + prio * 0x3F;
		line->bg3[x * 4 + 1] = line->bg3[x * 4 + 1] / 8 + prio * 0x3F;
		line->bg3[x * 4 + 2] = line->bg3[x * 4 + 2] / 8 + prio * 0x3F;
		prio = (line->obj[x * 4 + 3] >> 1) & 3;
		line->obj[x * 4 + 0] = line->obj[x * 4 + 0] / 8 + prio * 0x3F;
		line->obj[x * 4 + 1] = line->obj[x * 4 + 1] / 8 + prio * 0x3F;
		line->obj[x * 4 + 2] = line->obj[x * 4 + 2] / 8 + prio * 0x3F;
#endif
	}
	uint8_t *bg_data[4] = {&line->bg0[0], &line->bg1[0], &line->bg2[0], &line->bg3[0]};
	uint8_t bg_order[4];
	uint8_t bg_order_cnt = 0;
	uint8_t bg_prio[4];
	for (size_t i = 0; i < 4; ++i)
	{
		for (size_t j = 0; j < 4; ++j)
		{
			uint8_t bgp = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BG0CNT + 2 * j) & 3;
			if (bgp == i)
			{
				bg_order[bg_order_cnt] = j;
				bg_prio[bg_order_cnt] = bgp;
				bg_order_cnt++;
			}
		}
	}
	uint32_t dispcnt = eng_get_reg32(gpu, eng, MEM_ARM9_REG_DISPCNT);
	uint32_t has_window = dispcnt & (7 << 13);
	uint16_t bldcnt = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BLDCNT);
	uint8_t top_mask = (bldcnt >> 0) & 0x3F;
	uint8_t bot_mask = (bldcnt >> 8) & 0x3F;
	uint8_t blending = (bldcnt >> 6) & 3;
	uint16_t bldalpha = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BLDALPHA);
	uint8_t bldy = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BLDY) & 0x1F;
	uint8_t eva = (bldalpha >> 0) & 0x1F;
	uint8_t evb = (bldalpha >> 8) & 0x1F;
	dst = &eng->data[y * eng->pitch];
	for (size_t x = 0; x < 256; ++x, dst += 4)
	{
		uint8_t winflags;
		if (has_window)
			calcwindow(gpu, eng, line, x, y, &winflags);
		else
			winflags = 0xFF;
		uint8_t pixel_blend = blending;
		if (!(winflags & (1 << 5)))
			pixel_blend = 0;
		if (!pixel_blend && !(line->obj[x * 4 + 3] & 1))
		{
			enum layer_type layer = LAYER_BD;
			uint8_t priority = 4;
			for (size_t i = 0; i < bg_order_cnt; ++i)
			{
				uint8_t bgid = bg_order[i];
				if ((winflags & (1 << bgid)) && bg_data[bgid][x * 4 + 3])
				{
					layer = LAYER_BG0 + bgid;
					priority = bg_prio[i];
					break;
				}
			}
			if (winflags & (1 << 4))
			{
				uint8_t obj = line->obj[x * 4 + 3];
				if ((obj & 0x80) && ((obj >> 1) & 3) <= priority)
					layer = LAYER_OBJ;
			}
			*(uint32_t*)dst = *(uint32_t*)layer_data(line, layer, bd_color, x * 4, 0xFF);
			continue;
		}
		enum layer_type top_layer = LAYER_BD;
		enum layer_type bot_layer = LAYER_BD;
		uint8_t top_priority = 4;
		uint8_t bot_priority = 4;
		uint8_t alpha_obj_mask = 0;
		for (size_t i = 0; i < bg_order_cnt; ++i)
		{
			uint8_t bgid = bg_order[i];
			if (!(winflags & (1 << bgid)))
				continue;
			if (!bg_data[bgid][x * 4 + 3])
				continue;
			uint8_t prio = bg_prio[i];
			if (prio < top_priority)
			{
				bot_layer = top_layer;
				bot_priority = top_priority;
				top_layer = LAYER_BG0 + bgid;
				top_priority = prio;
				continue;
			}
			if (prio < bot_priority)
			{
				bot_layer = LAYER_BG0 + bgid;
				bot_priority = prio;
				continue;
			}
		}
		if (winflags & (1 << 4))
		{
			uint8_t obj = line->obj[x * 4 + 3];
			if (obj & 0x80)
			{
				uint8_t prio = (obj >> 1) & 3;
				if (prio <= top_priority)
				{
					bot_priority = top_priority;
					bot_layer = top_layer;
					top_priority = prio;
					top_layer = LAYER_OBJ;
					alpha_obj_mask = (obj & 1) << 4;
				}
				else if (prio <= bot_priority)
				{
					bot_priority = prio;
					bot_layer = LAYER_OBJ;
				}
			}
		}
		const uint8_t *top_layer_data = layer_data(line, top_layer, bd_color, x * 4, top_mask | alpha_obj_mask);
		const uint8_t *bot_layer_data = layer_data(line, bot_layer, bd_color, x * 4, bot_mask);
		if (alpha_obj_mask && bot_layer_data)
			pixel_blend = 1;
		switch (pixel_blend)
		{
			case 0:
				top_layer_data = layer_data(line, top_layer, bd_color, x * 4, 0xFF);
				*(uint32_t*)dst = *(uint32_t*)top_layer_data;
				break;
			case 1:
				if (top_layer_data && bot_layer_data)
				{
					for (size_t i = 0; i < 3; ++i)
					{
						uint16_t res = (top_layer_data[i] * eva + bot_layer_data[i] * evb) >> 4;
						if (res > 0xFF)
							res = 0xFF;
						dst[i] = res;
					}
				}
				else
				{
					top_layer_data = layer_data(line, top_layer, bd_color, x * 4, 0xFF);
					*(uint32_t*)dst = *(uint32_t*)top_layer_data;
				}
				break;
			case 2:
				if (top_layer_data)
				{
					for (size_t i = 0; i < 3; ++i)
						dst[i] = top_layer_data[i] + (((255 - top_layer_data[i]) * bldy) >> 4);
				}
				else
				{
					top_layer_data = layer_data(line, top_layer, bd_color, x * 4, 0xFF);
					*(uint32_t*)dst = *(uint32_t*)top_layer_data;
				}
				break;
			case 3:
				if (top_layer_data)
				{
					for (size_t i = 0; i < 3; ++i)
						dst[i] = top_layer_data[i] - ((top_layer_data[i] * bldy) >> 4);
				}
				else
				{
					top_layer_data = layer_data(line, top_layer, bd_color, x * 4, 0xFF);
					*(uint32_t*)dst = *(uint32_t*)top_layer_data;
				}
				break;
		}
	}
	uint16_t master_bright = eng_get_reg16(gpu, eng, MEM_ARM9_REG_MASTER_BRIGHT);
	switch ((master_bright >> 14) & 0x3)
	{
		case 0:
			break;
		case 1:
		{
			uint16_t factor = master_bright & 0x1F;
			if (factor > 16)
				factor = 16;
			uint8_t *ptr = &eng->data[y * eng->pitch];
			for (size_t x = 0; x < 256; ++x)
			{
				ptr[0] += ~ptr[0] * factor / 16;
				ptr[1] += ~ptr[1] * factor / 16;
				ptr[2] += ~ptr[2] * factor / 16;
				ptr += 4;
			}
			break;
		}
		case 2:
		{
			uint16_t factor = master_bright & 0x1F;
			if (factor > 16)
				factor = 16;
			uint8_t *ptr = &eng->data[y * eng->pitch];
			for (size_t x = 0; x < 256; ++x)
			{
				ptr[0] -= ptr[0] * factor / 16;
				ptr[1] -= ptr[1] * factor / 16;
				ptr[2] -= ptr[2] * factor / 16;
				ptr += 4;
			}
			break;
		}
	}
	switch (gpu->mem->spi_powerman.regs[0x4] & 0x3)
	{
		case 0:
		{
			uint8_t *ptr = &eng->data[y * eng->pitch];
			for (size_t x = 0; x < 256; ++x)
			{
				ptr[0] /= 4;
				ptr[1] /= 4;
				ptr[2] /= 4;
				ptr += 4;
			}
			break;
		}
		case 1:
		{
			uint8_t *ptr = &eng->data[y * eng->pitch];
			for (size_t x = 0; x < 256; ++x)
			{
				ptr[0] /= 2;
				ptr[1] /= 2;
				ptr[2] /= 2;
				ptr += 4;
			}
			break;
		}
		case 2:
		{
			uint8_t *ptr = &eng->data[y * eng->pitch];
			for (size_t x = 0; x < 256; ++x)
			{
				ptr[0] = ptr[0] / 2 + ptr[0] / 4;
				ptr[1] = ptr[1] / 2 + ptr[1] / 4;
				ptr[2] = ptr[2] / 2 + ptr[2] / 4;
				ptr += 4;
			}
			break;
		}
		case 3:
			break;
	}
}

static void draw_eng(struct gpu *gpu, struct gpu_eng *eng, uint8_t y)
{
	struct line_buff line;
	uint32_t dispcnt = eng_get_reg32(gpu, eng, MEM_ARM9_REG_DISPCNT);
#if 0
	printf("[ENG%c] DISPCNT: %08" PRIx32 "\n", eng->engb ? 'B' : 'A', dispcnt);
#endif
	switch ((dispcnt >> 16) & 0x3)
	{
		case 0:
			memset(&eng->data[y * eng->pitch], 0xFF, 256 * 4);
			return;
		case 1:
			break;
		case 2:
			printf("unhandled vram bitmap display\n");
			memset(&eng->data[y * eng->pitch], 0xFF, 256 * 4);
			return;
		case 3:
			printf("unhandled DMA vram bitmap display\n");
			memset(&eng->data[y * eng->pitch], 0xFF, 256 * 4);
			return;
	}
	memset(&line, 0, sizeof(line));
	switch (dispcnt & 0x7)
	{
		case 0:
			if (dispcnt & (1 << 0x8))
			{
				if (!eng->engb && (dispcnt & (1 << 3)))
					draw_background_3d(gpu, eng, y, 0, line.bg0);
				else
					draw_background_text(gpu, eng, y, 0, line.bg0);
			}
			if (dispcnt & (1 << 0x9))
				draw_background_text(gpu, eng, y, 1, line.bg1);
			if (dispcnt & (1 << 0xA))
				draw_background_text(gpu, eng, y, 2, line.bg2);
			if (dispcnt & (1 << 0xB))
				draw_background_text(gpu, eng, y, 3, line.bg3);
			break;
		case 1:
			if (dispcnt & (1 << 0x8))
			{
				if (!eng->engb && (dispcnt & (1 << 3)))
					draw_background_3d(gpu, eng, y, 0, line.bg0);
				else
					draw_background_text(gpu, eng, y, 0, line.bg0);
			}
			if (dispcnt & (1 << 0x9))
				draw_background_text(gpu, eng, y, 1, line.bg1);
			if (dispcnt & (1 << 0xA))
				draw_background_text(gpu, eng, y, 2, line.bg2);
			if (dispcnt & (1 << 0xB))
				draw_background_affine(gpu, eng, y, 3, line.bg3);
			break;
		case 2:
			if (dispcnt & (1 << 0x8))
			{
				if (!eng->engb && (dispcnt & (1 << 3)))
					draw_background_3d(gpu, eng, y, 0, line.bg0);
				else
					draw_background_text(gpu, eng, y, 0, line.bg0);
			}
			if (dispcnt & (1 << 0x9))
				draw_background_text(gpu, eng, y, 1, line.bg1);
			if (dispcnt & (1 << 0xA))
				draw_background_affine(gpu, eng, y, 2, line.bg2);
			if (dispcnt & (1 << 0xB))
				draw_background_affine(gpu, eng, y, 3, line.bg3);
			break;
		case 3:
			if (dispcnt & (1 << 0x8))
			{
				if (!eng->engb && (dispcnt & (1 << 3)))
					draw_background_3d(gpu, eng, y, 0, line.bg0);
				else
					draw_background_text(gpu, eng, y, 0, line.bg0);
			}
			if (dispcnt & (1 << 0x9))
				draw_background_text(gpu, eng, y, 1, line.bg1);
			if (dispcnt & (1 << 0xA))
				draw_background_text(gpu, eng, y, 2, line.bg2);
			if (dispcnt & (1 << 0xB))
				draw_background_extended(gpu, eng, y, 3, line.bg3);
			break;
		case 4:
			if (dispcnt & (1 << 0x8))
			{
				if (!eng->engb && (dispcnt & (1 << 3)))
					draw_background_3d(gpu, eng, y, 0, line.bg0);
				else
					draw_background_text(gpu, eng, y, 0, line.bg0);
			}
			if (dispcnt & (1 << 0x9))
				draw_background_text(gpu, eng, y, 1, line.bg1);
			if (dispcnt & (1 << 0xA))
				draw_background_affine(gpu, eng, y, 2, line.bg2);
			if (dispcnt & (1 << 0xB))
				draw_background_extended(gpu, eng, y, 3, line.bg3);
			break;
		case 5:
			if (dispcnt & (1 << 0x8))
			{
				if (!eng->engb && (dispcnt & (1 << 3)))
					draw_background_3d(gpu, eng, y, 0, line.bg0);
				else
					draw_background_text(gpu, eng, y, 0, line.bg0);
			}
			if (dispcnt & (1 << 0x9))
				draw_background_text(gpu, eng, y, 1, line.bg1);
			if (dispcnt & (1 << 0xA))
				draw_background_extended(gpu, eng, y, 2, line.bg2);
			if (dispcnt & (1 << 0xB))
				draw_background_extended(gpu, eng, y, 3, line.bg3);
			break;
		case 6:
			if (eng->engb)
			{
				printf("invalid mode 6 for engb\n");
				break;
			}
			if (dispcnt & (1 << 0x8))
				draw_background_3d(gpu, eng, y, 0, line.bg0);
			if (dispcnt & (1 << 0xA))
				draw_background_large(gpu, eng, y, 2, line.bg2);
			break;
		default:
			printf("invalid mode: %x\n", dispcnt & 0x7);
			return;
	}
	if (dispcnt & (1 << 0xC))
		draw_objects(gpu, eng, y, line.obj);
	compose(gpu, eng, &line, y);
	int16_t bg2pb = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BG2PB);
	int16_t bg2pd = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BG2PD);
	int16_t bg3pb = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BG3PB);
	int16_t bg3pd = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BG3PD);
	eng->bg2x += bg2pb;
	eng->bg2y += bg2pd;
	eng->bg3x += bg3pb;
	eng->bg3y += bg3pd;
}

void gpu_draw(struct gpu *gpu, uint8_t y)
{
	uint32_t powcnt1 = mem_arm9_get_reg32(gpu->mem, MEM_ARM9_REG_POWCNT1);
#if 0
	printf("powcnt1: %08" PRIx32 "\n", powcnt1);
#endif
	if (powcnt1 & (1 << 1))
		draw_eng(gpu, &gpu->enga, y);
	else
		memset(&gpu->enga.data[y * gpu->enga.pitch], 0, 256 * 4);
	if (powcnt1 & (1 << 9))
		draw_eng(gpu, &gpu->engb, y);
	else
		memset(&gpu->engb.data[y * gpu->engb.pitch], 0, 256 * 4);
}

static void eng_commit_bgpos(struct gpu *gpu, struct gpu_eng *eng)
{
	eng->bg2x = eng_get_reg32(gpu, eng, MEM_ARM9_REG_BG2X) & 0xFFFFFFF;
	eng->bg2y = eng_get_reg32(gpu, eng, MEM_ARM9_REG_BG2Y) & 0xFFFFFFF;
	eng->bg3x = eng_get_reg32(gpu, eng, MEM_ARM9_REG_BG3X) & 0xFFFFFFF;
	eng->bg3y = eng_get_reg32(gpu, eng, MEM_ARM9_REG_BG3Y) & 0xFFFFFFF;
	TRANSFORM_INT28(eng->bg2x);
	TRANSFORM_INT28(eng->bg2y);
	TRANSFORM_INT28(eng->bg3x);
	TRANSFORM_INT28(eng->bg3y);
}

void gpu_commit_bgpos(struct gpu *gpu)
{
	eng_commit_bgpos(gpu, &gpu->enga);
	eng_commit_bgpos(gpu, &gpu->engb);
}

static int32_t fp12_add(int32_t a, int32_t b)
{
	return a + b;
}

static int32_t fp12_mul(int32_t a, int32_t b)
{
	return ((int64_t)a * (int64_t)b) / (1 << 12);
}

static void mtx_mult(struct matrix *r, const struct matrix *a,
                     const struct matrix *b)
{
	int32_t *fr = (int32_t*)r;
	int32_t *fa = (int32_t*)a;
	int32_t *fb = (int32_t*)b;
	for (int y = 0; y < 4; ++y)
	{
		for (int x = 0; x < 4; ++x)
			fr[y + x * 4] = fp12_add(fp12_add(fp12_mul(fa[y + 0x0], fb[0 + x * 4]),
			                                  fp12_mul(fa[y + 0x4], fb[1 + x * 4])),
			                         fp12_add(fp12_mul(fa[y + 0x8], fb[2 + x * 4]),
			                                  fp12_mul(fa[y + 0xC], fb[3 + x * 4])));
	}
}

static void mtx_mult_vec4(struct vec4 *r, struct matrix *m, struct vec4 *v)
{
#define MULT_COMP(X) r->X = fp12_add(fp12_add(fp12_mul(v->x, m->x.X), \
                                              fp12_mul(v->y, m->y.X)), \
                                     fp12_add(fp12_mul(v->z, m->z.X), \
                                              fp12_mul(v->w, m->w.X)))
	MULT_COMP(x);
	MULT_COMP(y);
	MULT_COMP(z);
	MULT_COMP(w);
#undef MULT_COMP
}

static void mtx_mult_vec3(struct vec3 *r, struct matrix *m, struct vec3 *v)
{
#define MULT_COMP(X) r->X = fp12_add(fp12_add(fp12_mul(v->x, m->x.X), \
                                              fp12_mul(v->y, m->y.X)), \
                                     fp12_mul(v->z, m->z.X))
	MULT_COMP(x);
	MULT_COMP(y);
	MULT_COMP(z);
#undef MULT_COMP
}

static void mtx_mult_vec2(struct vec2 *r, struct matrix *m, struct vec2 *v)
{
#define MULT_COMP(X) r->X = fp12_add(fp12_mul(v->x, m->x.X), fp12_mul(v->y, m->y.X))
	MULT_COMP(x);
	MULT_COMP(y);
#undef MULT_COMP
}

static void mtx_print(const struct matrix *m)
{
	printf("{" I12_FMT" , " I12_FMT ", " I12_FMT ", " I12_FMT "}\n",
	       I12_PRT(m->x.x), I12_PRT(m->y.x), I12_PRT(m->z.x), I12_PRT(m->w.x));
	printf("{" I12_FMT" , " I12_FMT ", " I12_FMT ", " I12_FMT "}\n",
	       I12_PRT(m->x.y), I12_PRT(m->y.y), I12_PRT(m->z.y), I12_PRT(m->w.y));
	printf("{" I12_FMT" , " I12_FMT ", " I12_FMT ", " I12_FMT "}\n",
	       I12_PRT(m->x.z), I12_PRT(m->y.z), I12_PRT(m->z.z), I12_PRT(m->w.z));
	printf("{" I12_FMT" , " I12_FMT ", " I12_FMT ", " I12_FMT "}\n",
	       I12_PRT(m->x.w), I12_PRT(m->y.w), I12_PRT(m->z.w), I12_PRT(m->w.w));
}

static void update_clip_matrix(struct gpu *gpu)
{
	mtx_mult(&gpu->g3d.clip_matrix,
	         &gpu->g3d.proj_matrix,
	         &gpu->g3d.pos_matrix);
#if 1
	printf("proj:\n");
	mtx_print(&gpu->g3d.proj_matrix);
	printf("pos:\n");
	mtx_print(&gpu->g3d.pos_matrix);
	printf("clip:\n");
	mtx_print(&gpu->g3d.clip_matrix);
#endif
}

static void set_stack_error(struct gpu *gpu)
{
	mem_arm9_set_reg32(gpu->mem, MEM_ARM9_REG_GXSTAT,
	                   mem_arm9_get_reg32(gpu->mem, MEM_ARM9_REG_GXSTAT)
	                 | (1 << 15));
}

static void cmd_mtx_mode(struct gpu *gpu, uint32_t *params)
{
#if 1
	printf("[GX] MTX_MODE %" PRIu32 "\n", params[0] & 0x3);
#endif
	gpu->g3d.matrix_mode = params[0] & 0x3;
}

static void cmd_mtx_push(struct gpu *gpu, uint32_t *params)
{
#if 1
	printf("[GX] MTX_PUSH (mode=%" PRIu8 ")\n", gpu->g3d.matrix_mode);
#endif
	(void)params;
	switch (gpu->g3d.matrix_mode & 0x3)
	{
		case 0:
			memcpy(&gpu->g3d.proj_stack[0],
			       &gpu->g3d.proj_matrix,
			       sizeof(struct matrix));
			if (gpu->g3d.proj_stack_pos == 1)
			{
#if 1
				printf("[GX] MTX_PUSH proj stack overflow\n");
#endif
				set_stack_error(gpu);
				break;
			}
			gpu->g3d.proj_stack_pos++;
			mem_arm9_set_reg32(gpu->mem, MEM_ARM9_REG_GXSTAT,
			                   (mem_arm9_get_reg32(gpu->mem, MEM_ARM9_REG_GXSTAT)
			                  & ~(1 << 13))
			                  | (gpu->g3d.proj_stack_pos << 13));
			break;
		case 1:
		case 2:
			memcpy(&gpu->g3d.pos_stack[gpu->g3d.pos_stack_pos & 0x1F],
			       &gpu->g3d.pos_matrix,
			       sizeof(struct matrix));
			memcpy(&gpu->g3d.dir_stack[gpu->g3d.pos_stack_pos & 0x1F],
			       &gpu->g3d.dir_matrix,
			       sizeof(struct matrix));
			if (gpu->g3d.pos_stack_pos > 30)
			{
#if 1
				printf("[GX] MTX_PUSH pos stack overflow\n");
#endif
				set_stack_error(gpu);
			}
			gpu->g3d.pos_stack_pos++;
			gpu->g3d.pos_stack_pos &= 0x3F;
			mem_arm9_set_reg32(gpu->mem, MEM_ARM9_REG_GXSTAT,
			                   (mem_arm9_get_reg32(gpu->mem, MEM_ARM9_REG_GXSTAT)
			                  & ~(0x1F << 8))
			                  | (gpu->g3d.pos_stack_pos << 8));
			break;
		case 3:
			memcpy(&gpu->g3d.tex_stack[0],
			       &gpu->g3d.tex_matrix,
			       sizeof(struct matrix));
			if (gpu->g3d.tex_stack_pos == 1)
			{
#if 1
				printf("[GX] MTX_PUSH tex stack overflow\n");
#endif
				set_stack_error(gpu);
				break;
			}
			gpu->g3d.tex_stack_pos++;
			break;
	}
}

static void cmd_mtx_pop(struct gpu *gpu, uint32_t *params)
{
#if 1
	printf("[GX] MTX_POP 0x%02" PRIx32 " (mode=%" PRIu8 ")\n",
	       params[0], gpu->g3d.matrix_mode);
#endif
	uint32_t n = params[0] & 0x1F;
	switch (gpu->g3d.matrix_mode & 0x3)
	{
		case 0:
			if (!gpu->g3d.proj_stack_pos)
			{
#if 1
				printf("[GX] MTX_POP proj stack underflow\n");
#endif
				set_stack_error(gpu);
			}
			else
			{
				gpu->g3d.proj_stack_pos--;
			}
			memcpy(&gpu->g3d.proj_matrix,
			       &gpu->g3d.proj_stack[gpu->g3d.proj_stack_pos],
			       sizeof(struct matrix));
			update_clip_matrix(gpu);
			break;
		case 1:
		case 2:
			if (params[0] & (1 << 5))
			{
				n = (~n & 0x1F) + 1;
#if 1
				printf("[GX] MTX_POP negative: %" PRIu32 "\n", n);
#endif
				gpu->g3d.pos_stack_pos += n;
				if (gpu->g3d.pos_stack_pos > 30)
				{
#if 1
					printf("[GX] MTX_POP pos stack overflow\n");
#endif
					set_stack_error(gpu);
				}
				gpu->g3d.pos_stack_pos &= 0x3F;
			}
			else
			{
				if (n > gpu->g3d.pos_stack_pos)
				{
#if 1
					printf("[GX] MTX_POP pos stack underflow\n");
#endif
					set_stack_error(gpu);
					gpu->g3d.pos_stack_pos = 0;
				}
				else
				{
					gpu->g3d.pos_stack_pos -= n;
				}
			}
			memcpy(&gpu->g3d.pos_matrix,
			       &gpu->g3d.pos_stack[gpu->g3d.pos_stack_pos],
			       sizeof(struct matrix));
			memcpy(&gpu->g3d.dir_matrix,
			       &gpu->g3d.dir_stack[gpu->g3d.pos_stack_pos],
			       sizeof(struct matrix));
			update_clip_matrix(gpu);
			break;
		case 3:
			if (!gpu->g3d.tex_stack_pos)
			{
#if 1
				printf("[GX] MTX_POP tex stack underflow\n");
#endif
				set_stack_error(gpu);
			}
			else
			{
				gpu->g3d.tex_stack_pos--;
			}
			memcpy(&gpu->g3d.tex_matrix,
			       &gpu->g3d.tex_stack[gpu->g3d.tex_stack_pos],
			       sizeof(struct matrix));
			break;
	}
}

static void cmd_mtx_store(struct gpu *gpu, uint32_t *params)
{
#if 1
	printf("[GX] MTX_STORE 0x%02" PRIx32 " (mode=%" PRIu8 ")\n",
	       params[0], gpu->g3d.matrix_mode);
#endif
	uint32_t n = params[0] & 0x1F;
	switch (gpu->g3d.matrix_mode & 0x3)
	{
		case 0:
			memcpy(&gpu->g3d.proj_stack[0],
			       &gpu->g3d.proj_matrix,
			       sizeof(struct matrix));
			break;
		case 1:
		case 2:
			if (n == 0x1F)
			{
#if 1
				printf("[GX] MTX_STORE pos stack 0x1F\n");
#endif
				set_stack_error(gpu);
				break;
			}
			memcpy(&gpu->g3d.pos_stack[n],
			       &gpu->g3d.pos_matrix,
			       sizeof(struct matrix));
			memcpy(&gpu->g3d.dir_stack[n],
			       &gpu->g3d.dir_matrix,
			       sizeof(struct matrix));
			break;
		case 3:
			memcpy(&gpu->g3d.tex_stack[0],
			       &gpu->g3d.tex_matrix,
			       sizeof(struct matrix));
			break;
	}
}

static void cmd_mtx_restore(struct gpu *gpu, uint32_t *params)
{
#if 1
	printf("[GX] MTX_RESTORE 0x%02" PRIx32 " (mode=%" PRIu8 ")\n",
	       params[0], gpu->g3d.matrix_mode);
#endif
	uint32_t n = params[0] & 0x1F;
	switch (gpu->g3d.matrix_mode & 0x3)
	{
		case 0:
			memcpy(&gpu->g3d.proj_matrix,
			       &gpu->g3d.proj_stack[0],
			       sizeof(struct matrix));
			update_clip_matrix(gpu);
			break;
		case 1:
		case 2:
			if (n == 0x1F)
			{
#if 1
				printf("[GX] MTX_RESTORE pos stack 0x1F\n");
#endif
				set_stack_error(gpu);
				break;
			}
			memcpy(&gpu->g3d.pos_matrix,
			       &gpu->g3d.pos_stack[n],
			       sizeof(struct matrix));
			memcpy(&gpu->g3d.dir_matrix,
			       &gpu->g3d.dir_stack[n],
			       sizeof(struct matrix));
			update_clip_matrix(gpu);
			break;
		case 3:
			memcpy(&gpu->g3d.tex_matrix,
			       &gpu->g3d.tex_stack[0],
			       sizeof(struct matrix));
			break;
	}
}

static void load_identity(struct matrix *matrix)
{
	static const struct matrix identity_matrix =
	{
		{1 << 12, 0      , 0      , 0      },
		{0      , 1 << 12, 0      , 0      },
		{0      , 0      , 1 << 12, 0      },
		{0      , 0      , 0      , 1 << 12},
	};
	memcpy(matrix, &identity_matrix, sizeof(struct matrix));
}

static void cmd_mtx_identity(struct gpu *gpu, uint32_t *params)
{
#if 1
	printf("[GX] MTX_IDENTITY (mode=%" PRIu8 ")\n",
	       gpu->g3d.matrix_mode);
#endif
	(void)params;
	switch (gpu->g3d.matrix_mode & 0x3)
	{
		case 0:
			load_identity(&gpu->g3d.proj_matrix);
			update_clip_matrix(gpu);
			break;
		case 1:
			load_identity(&gpu->g3d.pos_matrix);
			update_clip_matrix(gpu);
			break;
		case 2:
			load_identity(&gpu->g3d.pos_matrix);
			load_identity(&gpu->g3d.dir_matrix);
			update_clip_matrix(gpu);
			break;
		case 3:
			load_identity(&gpu->g3d.tex_matrix);
			break;
	}
}

static void load_4x4(struct matrix *matrix, uint32_t *params)
{
	matrix->x.x = params[0x0];
	matrix->x.y = params[0x1];
	matrix->x.z = params[0x2];
	matrix->x.w = params[0x3];
	matrix->y.x = params[0x4];
	matrix->y.y = params[0x5];
	matrix->y.z = params[0x6];
	matrix->y.w = params[0x7];
	matrix->z.x = params[0x8];
	matrix->z.y = params[0x9];
	matrix->z.z = params[0xA];
	matrix->z.w = params[0xB];
	matrix->w.x = params[0xC];
	matrix->w.y = params[0xD];
	matrix->w.z = params[0xE];
	matrix->w.w = params[0xF];
}

static void cmd_mtx_load_4x4(struct gpu *gpu, uint32_t *params)
{
#if 1
	printf("[GX] MTX_LOAD_4X4 (mode=%" PRIu8 ")\n",
	       gpu->g3d.matrix_mode);
#endif
	switch (gpu->g3d.matrix_mode & 0x3)
	{
		case 0:
			load_4x4(&gpu->g3d.proj_matrix, params);
			update_clip_matrix(gpu);
			break;
		case 1:
			load_4x4(&gpu->g3d.pos_matrix, params);
			update_clip_matrix(gpu);
			break;
		case 2:
			load_4x4(&gpu->g3d.pos_matrix, params);
			load_4x4(&gpu->g3d.dir_matrix, params);
			update_clip_matrix(gpu);
			break;
		case 3:
			load_4x4(&gpu->g3d.tex_matrix, params);
			break;
	}
}

static void load_4x3(struct matrix *matrix, uint32_t *params)
{
	matrix->x.x = params[0x0];
	matrix->x.y = params[0x1];
	matrix->x.z = params[0x2];
	matrix->x.w = 0;
	matrix->y.x = params[0x3];
	matrix->y.y = params[0x4];
	matrix->y.z = params[0x5];
	matrix->y.w = 0;
	matrix->z.x = params[0x6];
	matrix->z.y = params[0x7];
	matrix->z.z = params[0x8];
	matrix->z.w = 0;
	matrix->w.x = params[0x9];
	matrix->w.y = params[0xA];
	matrix->w.z = params[0xB];
	matrix->w.w = (1 << 12);
}

static void cmd_mtx_load_4x3(struct gpu *gpu, uint32_t *params)
{
#if 1
	printf("[GX] MTX_LOAD_4X3 (mode=%" PRIu8 ")\n",
	       gpu->g3d.matrix_mode);
#endif
	switch (gpu->g3d.matrix_mode & 0x3)
	{
		case 0:
			load_4x3(&gpu->g3d.proj_matrix, params);
			update_clip_matrix(gpu);
			break;
		case 1:
			load_4x3(&gpu->g3d.pos_matrix, params);
			update_clip_matrix(gpu);
			break;
		case 2:
			load_4x3(&gpu->g3d.pos_matrix, params);
			load_4x3(&gpu->g3d.dir_matrix, params);
			update_clip_matrix(gpu);
			break;
		case 3:
			load_4x3(&gpu->g3d.tex_matrix, params);
			break;
	}
}

static void mult_4x4(struct matrix *a, struct matrix *b)
{
	struct matrix r;
	mtx_mult(&r, b, a);
	memcpy(a, &r, sizeof(r));
}

static void cmd_mtx_mult_4x4(struct gpu *gpu, uint32_t *paramms)
{
#if 1
	printf("[GX] MTX_MULT_4X4 (mode=%" PRIu8 ")\n",
	       gpu->g3d.matrix_mode);
#endif
	struct matrix matrix;
	load_4x4(&matrix, paramms);
	switch (gpu->g3d.matrix_mode & 0x3)
	{
		case 0:
			mult_4x4(&gpu->g3d.proj_matrix, &matrix);
			update_clip_matrix(gpu);
			break;
		case 1:
			mult_4x4(&gpu->g3d.pos_matrix, &matrix);
			update_clip_matrix(gpu);
			break;
		case 2:
			mult_4x4(&gpu->g3d.pos_matrix, &matrix);
			mult_4x4(&gpu->g3d.dir_matrix, &matrix);
			update_clip_matrix(gpu);
			break;
		case 3:
			mult_4x4(&gpu->g3d.tex_matrix, &matrix);
			break;
	}
}

static void cmd_mtx_mult_4x3(struct gpu *gpu, uint32_t *params)
{
#if 1
	printf("[GX] MTX_MULT_4X3 (mode=%" PRIu8 ")\n",
	       gpu->g3d.matrix_mode);
#endif
	struct matrix matrix;
	load_4x3(&matrix, params);
	switch (gpu->g3d.matrix_mode & 0x3)
	{
		case 0:
			mult_4x4(&gpu->g3d.proj_matrix, &matrix);
			update_clip_matrix(gpu);
			break;
		case 1:
			mult_4x4(&gpu->g3d.pos_matrix, &matrix);
			update_clip_matrix(gpu);
			break;
		case 2:
			mult_4x4(&gpu->g3d.pos_matrix, &matrix);
			mult_4x4(&gpu->g3d.dir_matrix, &matrix);
			update_clip_matrix(gpu);
			break;
		case 3:
			mult_4x4(&gpu->g3d.tex_matrix, &matrix);
			break;
	}
}

static void load_3x3(struct matrix *matrix, uint32_t *params)
{
	matrix->x.x = params[0x0];
	matrix->x.y = params[0x1];
	matrix->x.z = params[0x2];
	matrix->x.w = 0;
	matrix->y.x = params[0x3];
	matrix->y.y = params[0x4];
	matrix->y.z = params[0x5];
	matrix->y.w = 0;
	matrix->z.x = params[0x6];
	matrix->z.y = params[0x7];
	matrix->z.z = params[0x8];
	matrix->z.w = 0;
	matrix->w.x = 0;
	matrix->w.y = 0;
	matrix->w.z = 0;
	matrix->w.w = (1 << 12);
}

static void cmd_mtx_mult_3x3(struct gpu *gpu, uint32_t *params)
{
#if 1
	printf("[GX] MTX_MULT_3X3 (mode=%" PRIu8 ")\n",
	       gpu->g3d.matrix_mode);
#endif
	struct matrix matrix;
	load_3x3(&matrix, params);
	switch (gpu->g3d.matrix_mode & 0x3)
	{
		case 0:
			mult_4x4(&gpu->g3d.proj_matrix, &matrix);
			update_clip_matrix(gpu);
			break;
		case 1:
			mult_4x4(&gpu->g3d.pos_matrix, &matrix);
			update_clip_matrix(gpu);
			break;
		case 2:
			mult_4x4(&gpu->g3d.pos_matrix, &matrix);
			mult_4x4(&gpu->g3d.dir_matrix, &matrix);
			update_clip_matrix(gpu);
			break;
		case 3:
			mult_4x4(&gpu->g3d.tex_matrix, &matrix);
			break;
	}
}

static void mtx_scale(struct matrix *m, uint32_t *params)
{
	m->x.x = fp12_mul(m->x.x, (int32_t)params[0]);
	m->x.y = fp12_mul(m->x.y, (int32_t)params[0]);
	m->x.z = fp12_mul(m->x.z, (int32_t)params[0]);
	m->x.w = fp12_mul(m->x.w, (int32_t)params[0]);
	m->y.x = fp12_mul(m->y.x, (int32_t)params[1]);
	m->y.y = fp12_mul(m->y.y, (int32_t)params[1]);
	m->y.z = fp12_mul(m->y.z, (int32_t)params[1]);
	m->y.w = fp12_mul(m->y.w, (int32_t)params[1]);
	m->z.x = fp12_mul(m->z.x, (int32_t)params[2]);
	m->z.y = fp12_mul(m->z.y, (int32_t)params[2]);
	m->z.z = fp12_mul(m->z.z, (int32_t)params[2]);
	m->z.w = fp12_mul(m->z.w, (int32_t)params[2]);
}

static void cmd_mtx_scale(struct gpu *gpu, uint32_t *params)
{
#if 1
	printf("[GX] MTX_SCALE {" I12_FMT ", " I12_FMT ", " I12_FMT "} (mode=%" PRIu8 ")\n",
	       I12_PRT(params[0]), I12_PRT(params[1]), I12_PRT(params[2]),
	       gpu->g3d.matrix_mode);
#endif
	switch (gpu->g3d.matrix_mode & 0x3)
	{
		case 0:
			mtx_scale(&gpu->g3d.proj_matrix, params);
			update_clip_matrix(gpu);
			break;
		case 1:
		case 2:
			mtx_scale(&gpu->g3d.pos_matrix, params);
			update_clip_matrix(gpu);
			break;
		case 3:
			mtx_scale(&gpu->g3d.tex_matrix, params);
			break;
	}
}

static void mtx_trans(struct matrix *m, uint32_t *params)
{
	struct matrix tmp;
	tmp.x.x = (1 << 12);
	tmp.x.y = 0;
	tmp.x.z = 0;
	tmp.x.w = 0;
	tmp.y.x = 0;
	tmp.y.y = (1 << 12);
	tmp.y.z = 0;
	tmp.y.w = 0;
	tmp.z.x = 0;
	tmp.z.y = 0;
	tmp.z.z = (1 << 12);
	tmp.z.w = 0;
	tmp.w.x = params[0];
	tmp.w.y = params[1];
	tmp.w.z = params[2];
	tmp.w.w = (1 << 12);
	mult_4x4(m, &tmp);
}

static void cmd_mtx_trans(struct gpu *gpu, uint32_t *params)
{
#if 1
	printf("[GX] MTX_TRANS {" I12_FMT ", " I12_FMT ", " I12_FMT "} (mode=%" PRIu8 ")\n",
	       I12_PRT(params[0]), I12_PRT(params[1]), I12_PRT(params[2]),
	       gpu->g3d.matrix_mode);
#endif
	switch (gpu->g3d.matrix_mode & 0x3)
	{
		case 0:
			mtx_trans(&gpu->g3d.proj_matrix, params);
			update_clip_matrix(gpu);
			break;
		case 1:
		case 2:
			mtx_trans(&gpu->g3d.pos_matrix, params);
			update_clip_matrix(gpu);
			break;
		case 3:
			mtx_trans(&gpu->g3d.tex_matrix, params);
			break;
	}
}

static void cmd_color(struct gpu *gpu, uint32_t *params)
{
	gpu->g3d.r = TO8((params[0] >>  0) & 0x1F);
	gpu->g3d.g = TO8((params[0] >>  5) & 0x1F);
	gpu->g3d.b = TO8((params[0] >> 10) & 0x1F);
#if 1
	printf("[GX] COLOR {0x%02" PRIx8 ", 0x%02" PRIx8 ", 0x%02" PRIx8 "\n",
	       gpu->g3d.r, gpu->g3d.g, gpu->g3d.b);
#endif
}

static int32_t get_int10_9(uint16_t v)
{
	return ((int32_t)(int16_t)(v << 6)) / (1 << 3);
}

static void cmd_normal(struct gpu *gpu, uint32_t *params)
{
	struct vec3 normal;
	normal.x = get_int10_9((params[0] >>  0) & 0x3FF);
	normal.y = get_int10_9((params[0] >> 10) & 0x3FF);
	normal.z = get_int10_9((params[0] >> 20) & 0x3FF);
#if 1
	printf("[GX] NORMAL {" I12_FMT ", " I12_FMT ", " I12_FMT "}\n",
	       I12_PRT(normal.x), I12_PRT(normal.y), I12_PRT(normal.z));
#endif
	mtx_mult_vec3(&gpu->g3d.normal, &gpu->g3d.dir_matrix, &normal);
}

static int32_t get_int16_4(uint16_t v)
{
	return ((int32_t)(int16_t)v) * (1 << 8);
}

static void cmd_texcoord(struct gpu *gpu, uint32_t *params)
{
	struct vec2 texcoord;
	texcoord.x = get_int16_4((params[0] >>  0) & 0xFFFF);
	texcoord.y = get_int16_4((params[0] >> 16) & 0xFFFF);
#if 1
	printf("[GX] TEXCOORD {" I12_FMT ", " I12_FMT "}\n",
	       I12_PRT(texcoord.x), I12_PRT(texcoord.y));
#endif
	mtx_mult_vec2(&gpu->g3d.texcoord, &gpu->g3d.tex_matrix, &texcoord);
}

static void push_vertex(struct gpu *gpu)
{
	struct vertex *v = &gpu->g3d.vertexes[gpu->g3d.vertexes_nb];
	mtx_mult_vec4(&v->position, &gpu->g3d.clip_matrix, &gpu->g3d.position);
	v->r = gpu->g3d.r;
	v->g = gpu->g3d.g;
	v->b = gpu->g3d.b;
	v->normal = gpu->g3d.normal;
	v->texcoord = gpu->g3d.texcoord;
#if 1
	printf("[GX] push vertex {" I12_FMT ", " I12_FMT ", " I12_FMT "}\n",
	       I12_PRT(gpu->g3d.position.x),
	       I12_PRT(gpu->g3d.position.y),
	       I12_PRT(gpu->g3d.position.z));
	printf("     position: {" I12_FMT ", " I12_FMT ", " I12_FMT "}\n",
	       I12_PRT(v->position.x),
	       I12_PRT(v->position.y),
	       I12_PRT(v->position.z));
	printf("     color: {%" PRIu8 ", %" PRIu8 ", %" PRIu8 "}\n",
	       v->r, v->g, v->b);
	printf("     normal: {" I12_FMT ", " I12_FMT ", " I12_FMT "}\n",
	       I12_PRT(v->normal.x),
	       I12_PRT(v->normal.y),
	       I12_PRT(v->normal.z));
	printf("     texcoord: {" I12_FMT ", " I12_FMT "}\n",
	       I12_PRT(v->texcoord.x),
	       I12_PRT(v->texcoord.y));
#endif
}

static int32_t get_int16_12(uint16_t v)
{
	return (int32_t)(int16_t)v;
}

static void cmd_vtx_16(struct gpu *gpu, uint32_t *params)
{
	gpu->g3d.position.x = get_int16_12((params[0] >>  0) & 0xFFFF);
	gpu->g3d.position.y = get_int16_12((params[0] >> 16) & 0xFFFF);
	gpu->g3d.position.z = get_int16_12((params[1] >>  0) & 0xFFFF);
#if 1
	printf("[GX] VTX_16 {" I12_FMT ", " I12_FMT ", " I12_FMT "}\n",
	       I12_PRT(gpu->g3d.position.x),
	       I12_PRT(gpu->g3d.position.y),
	       I12_PRT(gpu->g3d.position.z));
#endif
	push_vertex(gpu);
}

static int32_t get_int10_6(uint16_t v)
{
	return (int32_t)(int16_t)(v << 6);
}

static void cmd_vtx_10(struct gpu *gpu, uint32_t *params)
{
	gpu->g3d.position.x = get_int10_6((params[0] >>  0) & 0x3FF);
	gpu->g3d.position.y = get_int10_6((params[0] >> 10) & 0x3FF);
	gpu->g3d.position.z = get_int10_6((params[0] >> 20) & 0x3FF);
#if 1
	printf("[GX] VTX_10 {" I12_FMT ", " I12_FMT ", " I12_FMT "}\n",
	       I12_PRT(gpu->g3d.position.x),
	       I12_PRT(gpu->g3d.position.y),
	       I12_PRT(gpu->g3d.position.z));
#endif
	push_vertex(gpu);
}

static void cmd_vtx_xy(struct gpu *gpu, uint32_t *params)
{
	gpu->g3d.position.x = get_int16_12((params[0] >>  0) & 0xFFFF);
	gpu->g3d.position.y = get_int16_12((params[0] >> 16) & 0xFFFF);
#if 1
	printf("[GX] VTX_XY {" I12_FMT ", " I12_FMT "}\n",
	       I12_PRT(gpu->g3d.position.x), I12_PRT(gpu->g3d.position.y));
#endif
	push_vertex(gpu);
}

static void cmd_vtx_xz(struct gpu *gpu, uint32_t *params)
{
	gpu->g3d.position.x = get_int16_12((params[0] >>  0) & 0xFFFF);
	gpu->g3d.position.z = get_int16_12((params[0] >> 16) & 0xFFFF);
#if 1
	printf("[GX] VTX_XZ {" I12_FMT ", " I12_FMT "}\n",
	       I12_PRT(gpu->g3d.position.x), I12_PRT(gpu->g3d.position.z));
#endif
	push_vertex(gpu);
}

static void cmd_vtx_yz(struct gpu *gpu, uint32_t *params)
{
	gpu->g3d.position.y = get_int16_12((params[0] >>  0) & 0xFFFF);
	gpu->g3d.position.z = get_int16_12((params[0] >> 16) & 0xFFFF);
#if 1
	printf("[GX] VTX_YZ {" I12_FMT ", " I12_FMT "}\n",
	       I12_PRT(gpu->g3d.position.y), I12_PRT(gpu->g3d.position.z));
#endif
	push_vertex(gpu);
}

static void cmd_vtx_diff(struct gpu *gpu, uint32_t *params)
{
#if 1
	printf("[GX] VTX_DIFF\n");
#endif
	gpu->g3d.position.x += get_int10_9((params[0] >>  0) & 0x3FF);
	gpu->g3d.position.y += get_int10_9((params[0] >> 10) & 0x3FF);
	gpu->g3d.position.z += get_int10_9((params[0] >> 20) & 0x3FF);
	push_vertex(gpu);
}

void gpu_gx_cmd(struct gpu *gpu, uint8_t cmd, uint32_t *params)
{
	switch (cmd)
	{
		case GX_CMD_MTX_MODE:
			cmd_mtx_mode(gpu, params);
			break;
		case GX_CMD_MTX_PUSH:
			cmd_mtx_push(gpu, params);
			break;
		case GX_CMD_MTX_POP:
			cmd_mtx_pop(gpu, params);
			break;
		case GX_CMD_MTX_STORE:
			cmd_mtx_store(gpu, params);
			break;
		case GX_CMD_MTX_RESTORE:
			cmd_mtx_restore(gpu, params);
			break;
		case GX_CMD_MTX_IDENTITY:
			cmd_mtx_identity(gpu, params);
			break;
		case GX_CMD_MTX_LOAD_4X4:
			cmd_mtx_load_4x4(gpu, params);
			break;
		case GX_CMD_MTX_LOAD_4X3:
			cmd_mtx_load_4x3(gpu, params);
			break;
		case GX_CMD_MTX_MULT_4X4:
			cmd_mtx_mult_4x4(gpu, params);
			break;
		case GX_CMD_MTX_MULT_4X3:
			cmd_mtx_mult_4x3(gpu, params);
			break;
		case GX_CMD_MTX_MULT_3X3:
			cmd_mtx_mult_3x3(gpu, params);
			break;
		case GX_CMD_MTX_SCALE:
			cmd_mtx_scale(gpu, params);
			break;
		case GX_CMD_MTX_TRANS:
			cmd_mtx_trans(gpu, params);
			break;
		case GX_CMD_COLOR:
			cmd_color(gpu, params);
			break;
		case GX_CMD_NORMAL:
			cmd_normal(gpu, params);
			break;
		case GX_CMD_TEXCOORD:
			cmd_texcoord(gpu, params);
			break;
		case GX_CMD_VTX_16:
			cmd_vtx_16(gpu, params);
			break;
		case GX_CMD_VTX_10:
			cmd_vtx_10(gpu, params);
			break;
		case GX_CMD_VTX_XY:
			cmd_vtx_xy(gpu, params);
			break;
		case GX_CMD_VTX_XZ:
			cmd_vtx_xz(gpu, params);
			break;
		case GX_CMD_VTX_YZ:
			cmd_vtx_yz(gpu, params);
			break;
		case GX_CMD_VTX_DIFF:
			cmd_vtx_diff(gpu, params);
			break;
		default:
			printf("[GX] unhandled gx cmd 0x%" PRIx8 "\n", cmd);
			break;
	}
}
