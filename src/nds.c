#include "nds.h"
#include "mbc.h"
#include "mem.h"
#include "apu.h"
#include "cpu.h"
#include "gpu.h"

#include <stdlib.h>
#include <string.h>

/*
 * 22A4: read a firmware byte
 * 227C: read an spi byte (return in r0, r0 = SPICNT (bit 15 not needed), r1 = hold)
 * 22D6: read spi bytes (r1 = dst, r2 = bytes count, r3 = unk (unused?))
 * 33A4: read spi bytes + WaitByLoop (r0 = dst, r1 = bytes count, r2 = unk (unused?))
 * 2330: function to read a block byte per byte (with cached data)
 *  - read + decrypt a firmware block into 0x037F90C4 (8 bytes, 0x037F800C as tmp buffer)
 *  - increment uint16_t @ 0x037F800A (if not 0 at call, no data is read + unciphered) (upper 29 bits are cleared (mod 8))
 *  - return 0x037F800C[*0x037F800A] in r0
 * 20BC: decrypt a block of 8 bytes (r0 = dst, r1 = src)
 * 1164: bios safe call
 * 1130: bios call wrapper of 20BC (by 1164)
 * 3344: thumb wrapper of 1130
 * 2388: (called 1 time) do things, then call 2330 4 times
 * 2A2A: LZ77UnCompReadByCallbackWrite16bit
 * 2462: calls LZ77UnCompReadByCallbackWrite16bit with:
 *             - open_and_get_32bit: 0x2388 (returns 0x010b4410, so LZ44 of 0x10b44 uncompressed bytes)
 *             - close             : 0x22C6
 *             - get_8bit          : 0x2330
 *             - get_16bit / get_32bit as null
 *
 *             read firmware up to CA18
 *       24A8: if (SVC_LZ77UnCompReadByCallbackWrite16bit(r0=0x0200, r1=0x02320000, r2=0x33E0, r3=0x33E0) > 0)
 *       24B6      SVC_GetCRC16(r0=0xFFFF, r1=0x02320000, r2=0x10B44) = 0x245F
 *
 *             read firmware up to 14F18
 *       24F2: if (SVC_LZ77UnCompReadBycallbackWrite16bit(r0=0xCA20, r1=0x037FA800, r2=0x33E0, r3=0x33E0) > 0)
 *       2500:     SVC_GetCRC16(r0=0x245F, r1=0x037FA800, r2=0x0B2B0) = 0x0F1F
 *
 *
 * - 2388
 *   - *
 *   - 2330
 *     - 33A4
 *       - 22D6
 *         - 227C
 *     - 20BC (indirect)
 *
 * - 2462
 *   - 2A2A
 *     - 2388
 */

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
			mem_timers(nds->mem);
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
