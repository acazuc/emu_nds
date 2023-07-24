#ifndef GPU_H
#define GPU_H

#include <stdint.h>

typedef struct mem mem_t;

typedef struct gpu
{
	struct gpu_eng
	{
		uint8_t data[256 * 192 * 4];
		uint32_t reg_base;
		uint32_t pal_base;
		uint32_t oam_base;
		uint32_t bg_base;
		uint32_t bg_mask;
		uint32_t obj_base;
		uint32_t obj_mask;
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
