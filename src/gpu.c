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

gpu_t *gpu_new(mem_t *mem)
{
	gpu_t *gpu = calloc(sizeof(*gpu), 1);
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

void gpu_del(gpu_t *gpu)
{
	if (!gpu)
		return;
	free(gpu);
}

static inline uint32_t eng_get_reg8(gpu_t *gpu, struct gpu_eng *eng, uint32_t reg)
{
	return mem_arm9_get_reg8(gpu->mem, reg + eng->reg_base);
}

static inline uint32_t eng_get_reg16(gpu_t *gpu, struct gpu_eng *eng, uint32_t reg)
{
	return mem_arm9_get_reg16(gpu->mem, reg + eng->reg_base);
}

static inline uint32_t eng_get_reg32(gpu_t *gpu, struct gpu_eng *eng, uint32_t reg)
{
	return mem_arm9_get_reg32(gpu->mem, reg + eng->reg_base);
}

static void draw_background_text(gpu_t *gpu, struct gpu_eng *eng, uint8_t y, uint8_t bg, uint8_t *data)
{
	static const uint32_t mapwidths[]  = {32 * 8, 64 * 8, 32 * 8, 64 * 8};
	static const uint32_t mapheights[] = {32 * 8, 32 * 8, 64 * 8, 64 * 8};
	uint32_t dispcnt = eng_get_reg32(gpu, eng, MEM_ARM9_REG_DISPCNT);
	uint16_t bgcnt = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BG0CNT + bg * 2);
	uint16_t bghofs = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BG0HOFS + bg * 4) & 0x1FF;
	uint16_t bgvofs = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BG0VOFS + bg * 4) & 0x1FF;
	uint8_t size = (bgcnt >> 14) & 0x3;
	uint32_t tilebase = ((bgcnt >> 2) & 0xF) * 0x4000;
	uint32_t mapbase = ((bgcnt >> 8) & 0x1F) * 0x800;
	if (!eng->engb)
	{
		tilebase += ((dispcnt >> 24) & 0x3) * 0x10000;
		mapbase += ((dispcnt >> 27) & 0x3) * 0x10000;
	}
#if 0
	printf("BGCNT: %04" PRIx16 ", tilebase: %08" PRIx32 ", mapbase: %08" PRIx32 "\n",
	       bgcnt, tilebase, mapbase);
#endif
	uint32_t mapw = mapwidths[size];
	uint32_t maph = mapheights[size];
	for (int32_t x = 0; x < 256; ++x)
	{
		int32_t vx = x + bghofs;
		int32_t vy = y + bgvofs;
		vx %= mapw;
		vy %= maph;
		if (vx < 0)
			vx += mapw;
		if (vy < 0)
			vy += maph;
		uint32_t mapx = vx / 8;
		uint32_t mapy = vy / 8;
		uint32_t tilex = vx % 8;
		uint32_t tiley = vy % 8;
		uint32_t mapoff = 0;
		if (mapy >= 32)
		{
			mapy -= 32;
			mapoff += 0x800;
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
#if 0
		printf("[BG0] %03ux%03u, tile: %04x (%08x)\n",
		       x, y, map, eng->bg_base + (mapaddr & eng->bg_mask));
#endif
		if (map & (1 << 10))
			tilex = 7 - tilex;
		if (map & (1 << 11))
			tiley = 7 - tiley;
		uint8_t paladdr;
		uint32_t tileaddr = tilebase;
		if (bgcnt & (1 << 7))
		{
			tileaddr += tileid * 0x40;
			tileaddr += tilex + tiley * 8;
			paladdr = eng->get_vram_bg8(gpu->mem, tileaddr);
			if (!paladdr)
				continue;
		}
		else
		{
			tileaddr += tileid * 0x20;
			tileaddr += tilex / 2 + tiley * 4;
			paladdr = eng->get_vram_bg8(gpu->mem, tileaddr);
#if 0
			printf("[BG0] %03ux%03u, pixel: %02x (%08x)\n",
			       x, y, paladdr, eng->bg_base + (tileaddr & eng->bg_mask));
#endif
			if (tilex & 1)
				paladdr >>= 4;
			else
				paladdr &= 0xF;
			if (!paladdr)
				continue;
			paladdr |= ((map >> 8) & 0xF0);
		}
		uint16_t val = mem_get_bg_palette(gpu->mem, eng->pal_base + paladdr * 2);
		SETRGB5(&data[x * 4], val, 0xFF);
	}
}

static void draw_background_affine(gpu_t *gpu, struct gpu_eng *eng, uint8_t y, uint8_t bg, uint8_t *data)
{
	(void)y;
	static const uint32_t mapsizes[]  = {16 * 8, 32 * 8, 64 * 8, 128 * 8};
	uint16_t bgcnt = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BG0CNT + bg * 2);
	uint8_t size = (bgcnt >> 14) & 0x3;
	uint32_t dispcnt = eng_get_reg32(gpu, eng, MEM_ARM9_REG_DISPCNT);
	uint32_t tilebase = ((bgcnt >> 2) & 0xF) * 0x4000 + ((dispcnt >> 24) & 0x3) * 0x10000;
	uint32_t mapbase = ((bgcnt >> 8) & 0x1F) * 0x800 + ((dispcnt >> 27) & 0x3) * 0x10000;
	uint32_t mapsize = mapsizes[size];
	uint8_t overflow = (bgcnt >> 13) & 1;
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
#if 0
		printf("[BG1] %03ux%03u, tileid: %03x\n", x, y, tileid);
#endif
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

static void draw_background_bitmap_3(gpu_t *gpu, struct gpu_eng *eng, uint8_t y, uint8_t *data)
{
	(void)y;
	int16_t pa = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BG2PA);
	int16_t pc = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BG2PC);
	int32_t bgx = eng->bg2x;
	int32_t bgy = eng->bg2y;
	for (int32_t x = 0; x < 256; ++x)
	{
		int32_t vx = bgx / 256;
		int32_t vy = bgy / 256;
		bgx += pa;
		bgy += pc;
		if (vx < 0 || vx >= 256
		 || vy < 0 || vy >= 192)
			continue;
		uint32_t addr = 2 * (vx + 256 * vy);
		uint16_t val = eng->get_vram_bg16(gpu->mem, addr);
		SETRGB5(&data[x * 4], val, 0xFF);
	}
}

static void draw_background_bitmap_4(gpu_t *gpu, struct gpu_eng *eng, uint8_t y, uint8_t *data)
{
	(void)y;
	int16_t pa = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BG2PA);
	int16_t pc = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BG2PC);
	int32_t bgx = eng->bg2x;
	int32_t bgy = eng->bg2y;
	uint32_t dispcnt = eng_get_reg32(gpu, eng, MEM_ARM9_REG_DISPCNT);
	uint32_t addr_offset = (dispcnt & (1 << 4)) ? 0xA000 : 0;
	for (int32_t x = 0; x < 256; ++x)
	{
		int32_t vx = bgx / 256;
		int32_t vy = bgy / 256;
		bgx += pa;
		bgy += pc;
		if (vx < 0 || vx >= 256
		 || vy < 0 || vy >= 192)
			continue;
		uint32_t addr = addr_offset + vx + 256 * vy;
		uint8_t val = eng->get_vram_bg8(gpu->mem, addr);
		if (!val)
			continue;
		uint16_t col = mem_get_bg_palette(gpu->mem, eng->pal_base + val * 2);
		SETRGB5(&data[x * 4], col, 0xFF);
	}
}

static void draw_background_bitmap_5(gpu_t *gpu, struct gpu_eng *eng, uint8_t y, uint8_t *data)
{
	if (y < 16 || y > 143)
	{
		memset(&data[0], 0, 4 * 256);
		return;
	}
	memset(&data[0], 0, 4 * 40);
	memset(&data[200 * 4], 0, 4 * 40);
	int16_t pa = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BG2PA);
	int16_t pc = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BG2PC);
	int32_t bgx = eng->bg2x;
	int32_t bgy = eng->bg2y;
	uint8_t baseaddr = (eng_get_reg32(gpu, eng, MEM_ARM9_REG_DISPCNT) & (1 << 4)) ? 0xA000 : 0;
	for (int32_t x = 0; x < 192; ++x)
	{
		int32_t vx = bgx / 256;
		int32_t vy = bgy / 256;
		bgx += pa;
		bgy += pc;
		if (vx < 0 || vx >= 160
		 || vy < 0 || vy >= 128)
			continue;
		uint32_t addr = baseaddr + 2 * (vx + 192 * vy);
		uint16_t val = eng->get_vram_bg16(gpu->mem, addr);
		SETRGB5(&data[(40 + x) * 4], val, 0xFF);
	}
}

static void draw_objects(gpu_t *gpu, struct gpu_eng *eng, uint8_t y, uint8_t *data)
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
			printf("unhandled obj3\n");
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
			uint16_t tilev = eng->get_vram_obj8(gpu->mem, tilepos * 0x20 + tileoff);
			if (!color_mode)
			{
				if (tilebx & 1)
					tilev >>= 4;
				else
					tilev &= 0xF;
			}
			if (!tilev)
				continue;
			if (!color_mode)
				tilev |= palette * 0x10;
			uint16_t col = mem_get_obj_palette(gpu->mem, eng->pal_base + tilev * 2);
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

static void calcwindow(gpu_t *gpu, struct gpu_eng *eng, struct line_buff *line, uint8_t x, uint8_t y, uint8_t *winflags)
{
	uint32_t dispcnt = eng_get_reg32(gpu, eng, MEM_ARM9_REG_DISPCNT);
	uint16_t win0h = eng_get_reg16(gpu, eng, MEM_ARM9_REG_WIN0H);
	uint16_t win0v = eng_get_reg16(gpu, eng, MEM_ARM9_REG_WIN0V);
	uint16_t win1h = eng_get_reg16(gpu, eng, MEM_ARM9_REG_WIN1H);
	uint16_t win1v = eng_get_reg16(gpu, eng, MEM_ARM9_REG_WIN1V);
	uint16_t winin = eng_get_reg16(gpu, eng, MEM_ARM9_REG_WININ);
	uint16_t winout = eng_get_reg16(gpu, eng, MEM_ARM9_REG_WINOUT);
	uint8_t win0l = win0h >> 8;
	uint8_t win0r = win0h & 0xFF;
	uint8_t win0t = win0v >> 8;
	uint8_t win0b = win0v & 0xFF;
	uint8_t win1l = win1h >> 8;
	uint8_t win1r = win1h & 0xFF;
	uint8_t win1t = win1v >> 8;
	uint8_t win1b = win1v & 0xFF;
	if (dispcnt & (1 << 13))
	{
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

static void compose(gpu_t *gpu, struct gpu_eng *eng, struct line_buff *line, uint8_t y)
{
	uint16_t bd_col = mem_get_bg_palette(gpu->mem, eng->pal_base + 0);
	uint8_t bd_color[4] = RGB5TO8(bd_col, 0xFF);
	for (size_t x = 0; x < 256; ++x)
	{
		memcpy(&eng->data[(256 * y + x) * 4], bd_color, 4);
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
	bool has_window = (dispcnt & (7 << 13)) != 0;
	uint16_t bldcnt = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BLDCNT);
	uint8_t top_mask = (bldcnt >> 0) & 0x3F;
	uint8_t bot_mask = (bldcnt >> 8) & 0x3F;
	uint8_t blending = (bldcnt >> 6) & 3;
	uint16_t bldalpha = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BLDALPHA);
	uint8_t bldy = eng_get_reg16(gpu, eng, MEM_ARM9_REG_BLDY) & 0x1F;
	uint8_t eva = (bldalpha >> 0) & 0x1F;
	uint8_t evb = (bldalpha >> 8) & 0x1F;
	for (size_t x = 0; x < 256; ++x)
	{
		uint8_t *dst = &eng->data[(y * 256 + x) * 4];
		uint8_t winflags;
		if (has_window)
		{
			calcwindow(gpu, eng, line, x, y, &winflags);
		}
		else
		{
			winflags = 0xFF;
		}
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
			memcpy(dst, layer_data(line, layer, bd_color, x * 4, 0xFF), 3);
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
				memcpy(dst, top_layer_data, 3);
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
					memcpy(dst, top_layer_data, 3);
				}
				break;
			case 2:
			{
				if (top_layer_data != NULL)
				{
					for (size_t i = 0; i < 3; ++i)
						dst[i] = top_layer_data[i] + (((255 - top_layer_data[i]) * bldy) >> 4);
				}
				else
				{
					top_layer_data = layer_data(line, top_layer, bd_color, x * 4, 0xFF);
					memcpy(dst, top_layer_data, 3);
				}
				break;
			}
			case 3:
			{
				if (top_layer_data != NULL)
				{
					for (size_t i = 0; i < 3; ++i)
						dst[i] = top_layer_data[i] - ((top_layer_data[i] * bldy) >> 4);
				}
				else
				{
					top_layer_data = layer_data(line, top_layer, bd_color, x * 4, 0xFF);
					memcpy(dst, top_layer_data, 3);
				}
				break;
			}
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
			for (size_t x = 0; x < 256; ++x)
			{
				uint8_t *ptr = &eng->data[(y * 256 + x) * 4];
				ptr[0] += ~ptr[0] * factor / 16;
				ptr[1] += ~ptr[1] * factor / 16;
				ptr[2] += ~ptr[2] * factor / 16;
			}
			break;
		}
		case 2:
		{
			uint16_t factor = master_bright & 0x1F;
			if (factor > 16)
				factor = 16;
			for (size_t x = 0; x < 256; ++x)
			{
				uint8_t *ptr = &eng->data[(y * 256 + x) * 4];
				ptr[0] -= ptr[0] * factor / 16;
				ptr[1] -= ptr[1] * factor / 16;
				ptr[2] -= ptr[2] * factor / 16;
			}
			break;
		}
	}
}

static void draw_eng(gpu_t *gpu, struct gpu_eng *eng, uint8_t y)
{
	struct line_buff line;
	uint32_t dispcnt = eng_get_reg32(gpu, eng, MEM_ARM9_REG_DISPCNT);
#if 0
	printf("[ENG%c] DISPCNT: %08" PRIx32 "\n", eng->engb ? 'B' : 'A', dispcnt);
#endif
	switch ((dispcnt >> 16) & 0x3)
	{
		case 0:
			memset(&eng->data[256 * 4 * y], 0xFF, 256 * 4);
			return;
		case 1:
			break;
		case 2:
			assert(!"unimp");
			break;
		case 3:
			assert(!"unimp");
			break;
	}
	memset(&line, 0, sizeof(line));
	switch (dispcnt & 0x7)
	{
		case 0:
			if (dispcnt & (1 << 0x8))
				draw_background_text(gpu, eng, y, 0, line.bg0);
			if (dispcnt & (1 << 0x9))
				draw_background_text(gpu, eng, y, 1, line.bg1);
			if (dispcnt & (1 << 0xA))
				draw_background_text(gpu, eng, y, 2, line.bg2);
			if (dispcnt & (1 << 0xB))
				draw_background_text(gpu, eng, y, 3, line.bg3);
			break;
		case 1:
			if (dispcnt & (1 << 0x8))
				draw_background_text(gpu, eng, y, 0, line.bg0);
			if (dispcnt & (1 << 0x9))
				draw_background_text(gpu, eng, y, 1, line.bg1);
			if (dispcnt & (1 << 0xA))
				draw_background_affine(gpu, eng, y, 2, line.bg2);
			break;
		case 2:
			if (dispcnt & (1 << 0xA))
				draw_background_affine(gpu, eng, y, 2, line.bg2);
			if (dispcnt & (1 << 0xB))
				draw_background_affine(gpu, eng, y, 3, line.bg3);
			break;
		case 3:
			if (dispcnt & (1 << 0xA))
				draw_background_bitmap_3(gpu, eng, y, line.bg2);
			break;
		case 4:
			if (dispcnt & (1 << 0xA))
				draw_background_bitmap_4(gpu, eng, y, line.bg2);
			break;
		case 5:
			if (dispcnt & (1 << 0xA))
				draw_background_bitmap_5(gpu, eng, y, line.bg2);
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

void gpu_draw(gpu_t *gpu, uint8_t y)
{
	uint32_t powcnt1 = mem_arm9_get_reg32(gpu->mem, MEM_ARM9_REG_POWCNT1);
#if 0
	printf("powcnt1: %08" PRIx32 "\n", powcnt1);
#endif
	if (powcnt1 & (1 << 1))
		draw_eng(gpu, &gpu->enga, y);
	else
		memset(&gpu->enga.data[y * 256 * 4], 0, 256 * 4);
	if (powcnt1 & (1 << 9))
		draw_eng(gpu, &gpu->engb, y);
	else
		memset(&gpu->engb.data[y * 256 * 4], 0, 256 * 4);
}

static void eng_commit_bgpos(gpu_t *gpu, struct gpu_eng *eng)
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

void gpu_commit_bgpos(gpu_t *gpu)
{
	eng_commit_bgpos(gpu, &gpu->enga);
	eng_commit_bgpos(gpu, &gpu->engb);
}
