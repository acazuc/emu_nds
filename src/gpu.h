#ifndef GPU_H
#define GPU_H

#include <stdint.h>

typedef struct mem mem_t;

typedef struct gpu
{
	struct gpu_eng
	{
		uint8_t data[256 * 192 * 4];
		uint32_t regoff;
		uint32_t paloff;
		uint32_t oamoff;
		uint32_t bgoff;
		uint32_t objoff;
		int32_t bg2x;
		int32_t bg2y;
		int32_t bg3x;
		int32_t bg3y;
	} enga, engb;
	mem_t *mem;
} gpu_t;

gpu_t *gpu_new(mem_t *mem);
void gpu_del(gpu_t *gpu);

void gpu_draw(gpu_t *gpu, uint8_t y);

#endif
