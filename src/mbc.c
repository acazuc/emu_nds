#include "mbc.h"
#include "nds.h"
#include "mem.h"
#include "cpu.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

mbc_t *mbc_new(nds_t *nds, const void *data, size_t size)
{
	mbc_t *mbc = calloc(sizeof(*mbc), 1);
	if (!mbc)
		return NULL;

	mbc->nds = nds;
	mbc->data = malloc(size);
	if (!mbc->data)
	{
		free(mbc);
		return NULL;
	}

	memcpy(mbc->data, data, size);
	mbc->data_size = size;
	return mbc;
}

void mbc_del(mbc_t *mbc)
{
	if (!mbc)
		return;
	free(mbc->data);
	free(mbc);
}

static void encrypt(mbc_t *mbc, uint32_t *data)
{
	uint32_t x = data[0];
	uint32_t y = data[1];
	for (size_t i = 0; i < 0x10; ++i)
	{
		uint32_t z = mbc->keybuf[i] ^ x;
		x  = mbc->keybuf[0x012 + ((z >> 24) & 0xFF)];
		x += mbc->keybuf[0x112 + ((z >> 16) & 0xFF)];
		x ^= mbc->keybuf[0x212 + ((z >>  8) & 0xFF)];
		x += mbc->keybuf[0x312 + ((z >>  0) & 0xFF)];
		x ^= y;
		y = z;
	}
	data[0] = x ^ mbc->keybuf[0x10];
	data[1] = y ^ mbc->keybuf[0x11];
}

static void decrypt(mbc_t *mbc, uint32_t *data)
{
	uint32_t x = data[0];
	uint32_t y = data[1];
	for (size_t i = 11; i > 0x1; --i)
	{
		uint32_t z = mbc->keybuf[i] ^ x;
		x  = mbc->keybuf[0x012 + ((z >> 24) & 0xFF)];
		x += mbc->keybuf[0x112 + ((z >> 16) & 0xFF)];
		x ^= mbc->keybuf[0x212 + ((z >>  8) & 0xFF)];
		x += mbc->keybuf[0x312 + ((z >>  0) & 0xFF)];
		x ^= y;
		y = z;
	}
	data[0] = x ^ mbc->keybuf[0x1];
	data[1] = y ^ mbc->keybuf[0x0];
}

static uint32_t bswap32(uint32_t v)
{
	return ((v >> 24) & 0x000000FF)
	     | ((v >>  8) & 0x0000FF00)
	     | ((v <<  8) & 0x00FF0000)
	     | ((v << 24) & 0xFF000000);
}

static void apply_keycode(mbc_t *mbc, uint32_t *keycode, uint8_t mod)
{
	encrypt(mbc, &keycode[1]);
	encrypt(mbc, &keycode[0]);
	for (size_t i = 0; i < 0x12; ++i)
		mbc->keybuf[i] ^= bswap32(mbc->keybuf[i % mod]);
	uint32_t scratch[2] = {0, 0};
	for (size_t i = 0; i < 0x410; i += 2)
	{
		encrypt(mbc, scratch);
		mbc->keybuf[i + 0] = scratch[1];
		mbc->keybuf[i + 1] = scratch[0];
	}
}

static void init_keycode(mbc_t *mbc, uint32_t idcode, uint8_t level, uint8_t mod)
{
	memcpy(mbc->keybuf, &mbc->nds->mem->arm7_bios[0x30], 0x1048);
	uint32_t keycode[3];
	keycode[0] = idcode;
	keycode[1] = idcode / 2;
	keycode[2] = idcode * 2;
	if (level >= 1)
		apply_keycode(mbc, keycode, mod);
	if (level >= 2)
		apply_keycode(mbc, keycode, mod);
	keycode[1] = keycode[1] * 2;
	keycode[2] = keycode[2] / 2;
	if (level >= 3)
		apply_keycode(mbc, keycode, mod);
}

void mbc_cmd(mbc_t *mbc)
{
	uint64_t cmd;
	cmd  = (uint64_t)mbc->nds->mem->arm9_regs[MEM_ARM9_REG_ROMCMD + 0] << 56;
	cmd |= (uint64_t)mbc->nds->mem->arm9_regs[MEM_ARM9_REG_ROMCMD + 1] << 48;
	cmd |= (uint64_t)mbc->nds->mem->arm9_regs[MEM_ARM9_REG_ROMCMD + 2] << 40;
	cmd |= (uint64_t)mbc->nds->mem->arm9_regs[MEM_ARM9_REG_ROMCMD + 3] << 32;
	cmd |= (uint64_t)mbc->nds->mem->arm9_regs[MEM_ARM9_REG_ROMCMD + 4] << 24;
	cmd |= (uint64_t)mbc->nds->mem->arm9_regs[MEM_ARM9_REG_ROMCMD + 5] << 16;
	cmd |= (uint64_t)mbc->nds->mem->arm9_regs[MEM_ARM9_REG_ROMCMD + 6] << 8;
	cmd |= (uint64_t)mbc->nds->mem->arm9_regs[MEM_ARM9_REG_ROMCMD + 7] << 0;
#if 1
	printf("CMD %016" PRIx64 "\n", cmd);
#endif
	switch (mbc->enc)
	{
		case 0:
			switch ((cmd >> 56) & 0xFF)
			{
				case 0x9F:
					assert(cmd == 0x9F00000000000000ULL);
					mbc->cmd = MBC_CMD_DUMMY;
					mbc->cmd_data.dummy.count = 0x2000;
					mbc->nds->mem->arm9_regs[MEM_ARM9_REG_ROMCTRL + 2] |= (1 << 7); /* XXX another way */
					return;
				case 0x00:
					assert(cmd == 0x0000000000000000ULL);
					mbc->cmd = MBC_CMD_GETHDR;
					mbc->cmd_data.gethdr.count = 0;
					mbc->nds->mem->arm9_regs[MEM_ARM9_REG_ROMCTRL + 2] |= (1 << 7); /* XXX another way */
					return;
				case 0x90:
					assert(cmd == 0x9000000000000000ULL);
					mbc->cmd = MBC_CMD_ROMID1;
					mbc->cmd_data.gethdr.count = 0;
					mbc->nds->mem->arm9_regs[MEM_ARM9_REG_ROMCTRL + 2] |= (1 << 7); /* XXX another way */
					mbc->nds->arm7->debug = CPU_DEBUG_ALL | CPU_DEBUG_REGS_ML;
					return;
				case 0x3C:
					mbc->enc = 1;
					init_keycode(mbc, ((uint32_t*)mbc->data)[0x3], 1, 8);
					mbc->nds->mem->arm9_regs[MEM_ARM9_REG_ROMCTRL + 3] &= ~(1 << 7); /* XXX another way */
					return;
				default:
					assert(!"unknown command");
					return;
			}
			break;
		case 1:
		{
			uint32_t values[2];
			values[1] = bswap32(cmd >> 32);
			values[0] = bswap32(cmd >>  0);
			decrypt(mbc, values);
			uint32_t tmp = bswap32(values[0]);
			values[0] = bswap32(values[1]);
			values[1] = tmp;
			printf("decrypted: %08x %08x\n", values[0], values[1]);
			break;
		}
	}
}

uint8_t mbc_read(mbc_t *mbc)
{
	switch (mbc->cmd)
	{
		case MBC_CMD_NONE:
			return 0;
		case MBC_CMD_DUMMY:
#if 1
			printf("dummy 0x%" PRIx32 "\n", mbc->cmd_data.dummy.count);
#endif
			if (!--mbc->cmd_data.dummy.count)
			{
				mbc->cmd = MBC_CMD_NONE;
				mbc->nds->mem->arm9_regs[MEM_ARM9_REG_ROMCTRL + 3] &= ~(1 << 7); /* XXX another way */
			}
			return 0xFF;
		case MBC_CMD_GETHDR:
		{
#if 1
			printf("gethdr 0x%" PRIx32 "\n", mbc->cmd_data.gethdr.count);
#endif
			if (mbc->cmd_data.gethdr.count == 0x1FF)
			{
				mbc->cmd = MBC_CMD_NONE;
				mbc->nds->mem->arm9_regs[MEM_ARM9_REG_ROMCTRL + 3] &= ~(1 << 7); /* XXX another way */
			}
			uint8_t v;
			if (mbc->cmd_data.gethdr.count < mbc->data_size)
				v = mbc->data[mbc->cmd_data.gethdr.count];
			else
				v = 0;
			mbc->cmd_data.gethdr.count++;
			return v;
		}
		case MBC_CMD_ROMID1:
#if 1
			printf("romid1 0x%" PRIx32 "\n", mbc->cmd_data.romid1.count);
#endif
			switch (mbc->cmd_data.romid1.count)
			{
				case 0:
					mbc->cmd_data.romid1.count++;
					return 0xC2;
				case 1:
					mbc->cmd_data.romid1.count++;
					return 0x00;
				case 2:
					mbc->cmd_data.romid1.count++;
					return 0x00;
				case 3:
					mbc->cmd = MBC_CMD_NONE;
					mbc->nds->mem->arm9_regs[MEM_ARM9_REG_ROMCTRL + 3] &= ~(1 << 7); /* XXX another way */
					return 0x00;
			}
			assert(!"dead");
			return 0;
		default:
			assert(!"unknown cmd");
	}
}

void mbc_write(mbc_t *mbc, uint8_t v)
{
	switch (mbc->cmd)
	{
		case MBC_CMD_NONE:
			return;
		case MBC_CMD_DUMMY:
			assert(!"write dummy cmd");
			return;
		default:
			assert(!"unknown cmd");
	}
}
