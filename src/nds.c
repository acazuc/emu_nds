#include "nds.h"
#include "mbc.h"
#include "mem.h"
#include "apu.h"
#include "cpu.h"
#include "gpu.h"

#include <stdlib.h>
#include <string.h>

nds_t *nds_new(const void *rom_data, size_t rom_size)
{
	nds_t *nds = calloc(sizeof(*nds), 1);
	if (!nds)
		return NULL;

	nds->mbc = mbc_new(nds, rom_data, rom_size);
	if (!nds->mbc)
		return NULL;

	nds->mem = mem_new(nds, nds->mbc);
	if (!nds->mem)
		return NULL;

	nds->apu = apu_new(nds->mem);
	if (!nds->apu)
		return NULL;

	nds->arm7 = cpu_new(nds->mem, 0);
	if (!nds->arm7)
		return NULL;

	nds->arm9 = cpu_new(nds->mem, 1);
	if (!nds->arm9)
		return NULL;

	nds->gpu = gpu_new(nds->mem);
	if (!nds->gpu)
		return NULL;

	return nds;
}

void nds_del(nds_t *nds)
{
	if (!nds)
		return;
	mbc_del(nds->mbc);
	mem_del(nds->mem);
	apu_del(nds->apu);
	cpu_del(nds->arm7);
	cpu_del(nds->arm9);
	gpu_del(nds->gpu);
	free(nds);
}

static void nds_cycles(nds_t *nds, uint32_t cycles)
{
	for (; cycles; --cycles)
	{
		nds->cycle++;
		if (nds->cycle & 1)
		{
			if (!nds->arm7->instr_delay)
				cpu_cycle(nds->arm7);
			else
				nds->arm7->instr_delay--;
		}
		if (!nds->arm9->instr_delay)
			cpu_cycle(nds->arm9);
		else
			nds->arm9->instr_delay--;
		apu_cycle(nds->apu);
	}
}

void nds_frame(nds_t *nds, uint8_t *video_buf, int16_t *audio_buf, uint32_t joypad)
{
	nds->joypad = joypad;
	for (uint8_t y = 0; y < 192; ++y)
	{
		/* draw */
		gpu_draw(nds->gpu, y);
		nds_cycles(nds, 256 * 12);

		/* hblank */

		nds_cycles(nds, 99 * 12);
	}

	for (uint16_t y = 192; y < 263; ++y)
	{
		/* vblank */
		nds_cycles(nds, 256 * 12);

		/* hblank */

		nds_cycles(nds, 99 * 12);
	}

	memcpy(video_buf, nds->gpu->data, sizeof(nds->gpu->data));
	memcpy(audio_buf, nds->apu->data, sizeof(nds->apu->data));
}

void nds_set_arm7_bios(nds_t *nds, const uint8_t *data)
{
	memcpy(nds->mem->arm7_bios, data, 0x4000);
}

void nds_set_arm9_bios(nds_t *nds, const uint8_t *data)
{
	memcpy(nds->mem->arm9_bios, data, 0x1000);
}

void nds_set_firmware(nds_t *nds, const uint8_t *data)
{
	memcpy(nds->mem->firmware, data, 0x40000);
}

void nds_get_mbc_ram(nds_t *nds, uint8_t **data, size_t *size)
{
	(void)nds;
	*data = NULL;
	*size = 0;
}

void nds_get_mbc_rtc(nds_t *nds, uint8_t **data, size_t *size)
{
	(void)nds;
	*data = NULL;
	*size = 0;
}
