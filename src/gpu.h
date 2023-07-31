#ifndef GPU_H
#define GPU_H

#include <stdint.h>

#define TRANSFORM_INT28(n) \
do \
{ \
	if ((n) & (1 << 27)) \
	{ \
		n = (0x7FFFFFF - ((n) & 0x7FFFFFF)); \
		n = -n - 1; \
	} \
} while (0)

struct mem;

struct gpu_eng
{
	uint8_t *data;
	uint8_t  (*get_vram_bg8 )(struct mem *mem, uint32_t addr);
	uint16_t (*get_vram_bg16)(struct mem *mem, uint32_t addr);
	uint32_t (*get_vram_bg32)(struct mem *mem, uint32_t addr);
	uint8_t  (*get_vram_obj8 )(struct mem *mem, uint32_t addr);
	uint16_t (*get_vram_obj16)(struct mem *mem, uint32_t addr);
	uint32_t (*get_vram_obj32)(struct mem *mem, uint32_t addr);
	uint32_t reg_base;
	uint32_t pal_base;
	uint32_t oam_base;
	int32_t bg2x;
	int32_t bg2y;
	int32_t bg3x;
	int32_t bg3y;
	int engb;
};

struct gpu
{
	struct gpu_eng enga;
	struct gpu_eng engb;
	struct mem *mem;
};

struct gpu *gpu_new(struct mem *mem);
void gpu_del(struct gpu *gpu);

void gpu_draw(struct gpu *gpu, uint8_t y);
void gpu_commit_bgpos(struct gpu *gpu);

#endif
