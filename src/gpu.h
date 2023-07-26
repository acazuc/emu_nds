#ifndef GPU_H
#define GPU_H

#include <stdint.h>

typedef struct mem mem_t;

typedef struct gpu
{
	struct gpu_eng
	{
		uint8_t data[256 * 192 * 4];
		uint8_t  (*get_vram_bg8 )(mem_t *mem, uint32_t addr);
		uint16_t (*get_vram_bg16)(mem_t *mem, uint32_t addr);
		uint32_t (*get_vram_bg32)(mem_t *mem, uint32_t addr);
		uint8_t  (*get_vram_obj8 )(mem_t *mem, uint32_t addr);
		uint16_t (*get_vram_obj16)(mem_t *mem, uint32_t addr);
		uint32_t (*get_vram_obj32)(mem_t *mem, uint32_t addr);
		uint32_t reg_base;
		uint32_t pal_base;
		uint32_t oam_base;
		int32_t bg2x;
		int32_t bg2y;
		int32_t bg3x;
		int32_t bg3y;
		int engb;
	} enga, engb;
	mem_t *mem;
} gpu_t;

gpu_t *gpu_new(mem_t *mem);
void gpu_del(gpu_t *gpu);

void gpu_draw(gpu_t *gpu, uint8_t y);

#endif
