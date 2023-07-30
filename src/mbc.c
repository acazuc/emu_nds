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
	mbc->chipid[0] = 0xC2;
	mbc->chipid[1] = size / (1024 * 1024) - 1;
	mbc->chipid[2] = 0x00;
	mbc->chipid[3] = 0x00;
	switch (((uint32_t*)data)[3])
	{
		case 0x50434D41:
			mbc->backup_type = MBC_FLASH_256K;
			break;
		default:
			mbc->backup_type = MBC_BACKUP_UNKNOWN;
			printf("unknown backup for gamecode %08" PRIx32 "\n",
			       ((uint32_t*)data)[3]);
			break;
	}
	switch (mbc->backup_type)
	{
		case MBC_BACKUP_UNKNOWN:
			break;
		case MBC_EEPROM_512:
			mbc->backup_size = 512;
			break;
		case MBC_EEPROM_8K:
			mbc->backup_size = 8 * 1024;
			break;
		case MBC_EEPROM_64K:
			mbc->backup_size = 64 * 1024;
			break;
		case MBC_EEPROM_128K:
			mbc->backup_size = 128 * 1024;
			break;
		case MBC_FLASH_256K:
			mbc->backup_size = 256 * 1024;
			break;
		case MBC_FLASH_512K:
			mbc->backup_size = 512 * 1024;
			break;
		case MBC_FLASH_1024K:
			mbc->backup_size = 1024 * 1024;
			break;
		case MBC_FLASH_2048K:
			mbc->backup_size = 2048 * 1024;
			break;
		case MBC_FRAM_8K:
			mbc->backup_size = 8 * 1024;
			break;
		case MBC_FRAM_32K:
			mbc->backup_size = 32 * 1024;
			break;
	}
	return mbc;
}

void mbc_del(mbc_t *mbc)
{
	if (!mbc)
		return;
	free(mbc->data);
	free(mbc);
}

static void encrypt(mbc_t *mbc, void *data)
{
	uint32_t x = ((uint32_t*)data)[1];
	uint32_t y = ((uint32_t*)data)[0];
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
	((uint32_t*)data)[0] = x ^ mbc->keybuf[0x10];
	((uint32_t*)data)[1] = y ^ mbc->keybuf[0x11];
}

static void decrypt(mbc_t *mbc, uint32_t *data)
{
	uint32_t x = data[1];
	uint32_t y = data[0];
	for (size_t i = 0x11; i > 0x1; --i)
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
		mbc->keybuf[i] ^= bswap32(keycode[i % mod]);
	uint32_t scratch[2] = {0, 0};
	for (size_t i = 0; i < 0x412; i += 2)
	{
		encrypt(mbc, &scratch[0]);
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

static void start_cmd(mbc_t *mbc)
{
#if 0
	printf("start cmd with ROMCTRL=%08" PRIx32 ", AUXSPICNT=%04" PRIx32 "\n",
	       mem_arm9_get_reg32(mbc->nds->mem, MEM_ARM9_REG_ROMCTRL),
	       mem_arm9_get_reg16(mbc->nds->mem, MEM_ARM9_REG_AUXSPICNT));
#endif
	mbc->nds->mem->arm9_regs[MEM_ARM9_REG_ROMCTRL + 2] |= (1 << 7); /* XXX another way */
	mem_dscard(mbc->nds->mem);
}

static void end_cmd(mbc_t *mbc)
{
#if 0
	printf("end cmd %s interrupt\n", (mem_arm9_get_reg16(mbc->nds->mem, MEM_ARM9_REG_AUXSPICNT) & (1 << 14)) ? "with" : "without");
#endif
	mbc->cmd = MBC_CMD_NONE;
	mbc->nds->mem->arm9_regs[MEM_ARM9_REG_ROMCTRL + 3] &= ~(1 << 7); /* XXX another way */
	if (mem_arm9_get_reg16(mbc->nds->mem, MEM_ARM9_REG_AUXSPICNT) & (1 << 14))
	{
		mem_arm9_if(mbc->nds->mem, 1 << 19);
		mem_arm7_if(mbc->nds->mem, 1 << 19);
	}
}

static uint64_t bitswap39(uint64_t v)
{
	uint64_t ret = 0;
	for (size_t i = 0; i < 39; ++i)
		ret |= ((v >> i) & 1) << (38 - i);
	return ret;
}

static uint8_t key2_byte(mbc_t *mbc, uint8_t v)
{
	return v; /* it looks like it's not required... is it a hw-only cipher ? */
	mbc->key2_x = ((((mbc->key2_x >> 5) ^ (mbc->key2_x >> 17) ^ (mbc->key2_x >> 18) ^ (mbc->key2_x >> 31)) & 0xFF) | (mbc->key2_x << 8)) & 0x7FFFFFFFFF;
	mbc->key2_y = ((((mbc->key2_y >> 5) ^ (mbc->key2_y >> 23) ^ (mbc->key2_y >> 18) ^ (mbc->key2_y >> 31)) & 0xFF) | (mbc->key2_y << 8)) & 0x7FFFFFFFFF;
	return v ^ mbc->key2_x ^ mbc->key2_y;
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
#if 0
	uint32_t romctrl = mem_arm9_get_reg32(mbc->nds->mem, MEM_ARM9_REG_ROMCTRL);
	printf("CMD %016" PRIx64 " with ROMCTRL 0x%08" PRIx32 "\n",
	       cmd, romctrl);
#if 1
	uint32_t length = (romctrl >> 24) & 0x7;
	if (!length)
		printf("length: 0\n");
	else if (length == 7)
		printf("length: 4\n");
	else
		printf("length: 0x%x\n", 0x100 << length);
#endif
#endif
	switch (mbc->enc)
	{
		case 0:
			switch ((cmd >> 56) & 0xFF)
			{
				case 0x9F:
					assert(cmd == 0x9F00000000000000ULL);
					mbc->cmd = MBC_CMD_DUMMY;
					mbc->cmd_count = 0x2000;
					start_cmd(mbc);
					return;
				case 0x00:
					assert(cmd == 0x0000000000000000ULL);
					mbc->cmd = MBC_CMD_GETHDR;
					mbc->cmd_count = 0;
					start_cmd(mbc);
					return;
				case 0x90:
					assert(cmd == 0x9000000000000000ULL);
					mbc->cmd = MBC_CMD_ROMID1;
					mbc->cmd_count = 0;
					start_cmd(mbc);
					return;
				case 0x3C:
					mbc->enc = 1;
					init_keycode(mbc, ((uint32_t*)mbc->data)[0x3], 3, 2);
					memcpy(mbc->secure_area, "encryObj", 8);
					memcpy(&mbc->secure_area[0x8], &mbc->data[0x4008], 0x7F8);
					for (size_t i = 0; i < 0x800; i += 8)
						encrypt(mbc, &mbc->secure_area[i]);
					init_keycode(mbc, ((uint32_t*)mbc->data)[0x3], 2, 2);
					encrypt(mbc, &mbc->secure_area[0]);
					end_cmd(mbc);
					return;
				default:
					printf("MBC unknown command: 0x%02" PRIx8 "\n",
					       (uint8_t)(cmd >> 56));
					return;
			}
			break;
		case 1:
		{
			uint32_t values[2];
			values[1] = cmd >> 32;
			values[0] = cmd >> 0;
			decrypt(mbc, values);
			cmd = ((uint64_t)values[1] << 32) | values[0];
#if 0
			printf("CMD KEY1 %016" PRIx64 "\n", cmd);
#endif
			switch ((cmd >> 60) & 0xF)
			{
				case 0x4:
				{
					static const uint8_t seeds[] =
					{
						0xE8, 0x4D, 0x5A, 0xB1,
						0x17, 0x8F, 0x99, 0xD5,
					};
					mbc->key2_x = bitswap39(((cmd & 0xFFFFFF00000) >> 5) | 0x6000 | seeds[mbc->data[0x13] & 0x7]);
					mbc->key2_y = bitswap39(0x5C879B9B05);
					end_cmd(mbc);
					return;
				}
				case 0x1:
					mbc->cmd = MBC_CMD_ROMID2;
					mbc->cmd_count = 0;
					start_cmd(mbc);
					return;
				case 0x2:
					mbc->cmd = MBC_CMD_SECBLK;
					mbc->cmd_count = 0;
					mbc->cmd_off = 0x1000 * ((cmd >> 44) & 0xFFF);
					start_cmd(mbc);
					return;
				case 0xA:
					mbc->enc = 2;
					end_cmd(mbc);
					return;
				default:
					printf("MBC unknown KEY1 command: 0x%02" PRIx8 "\n",
					       (uint8_t)(cmd >> 60));
					return;
			}
			break;
		}
		case 2:
		{
			uint64_t cmd_dec = 0;
			for (size_t i = 0; i < 64; i += 8)
				cmd_dec |= ((uint64_t)key2_byte(mbc, cmd >> i)) << i;
#if 0
			printf("CMD KEY2 %016" PRIx64 "\n", cmd_dec);
#endif
			switch ((cmd >> 56) & 0xFF)
			{
				case 0xB7:
					mbc->cmd = MBC_CMD_ENCREAD;
					mbc->cmd_count = 0;
					mbc->cmd_off = (cmd >> 24) & 0xFFFFFFFF;
#if 0
					printf("MBC read 0x%08" PRIx32 "\n", mbc->cmd_off);
#endif
					start_cmd(mbc);
					return;
				case 0xB8:
					mbc->cmd = MBC_CMD_ROMID2;
					mbc->cmd_count = 0;
					start_cmd(mbc);
					return;
				default:
					printf("MBC unknown KEY2 command: 0x%02" PRIx8 "\n",
					       (uint8_t)(cmd >> 60));
					return;
			}
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
#if 0
			printf("dummy 0x%" PRIx32 "\n", mbc->cmd_count);
#endif
			if (!--mbc->cmd_count)
				end_cmd(mbc);
			return 0xFF;
		case MBC_CMD_GETHDR:
		{
#if 0
			printf("gethdr 0x%" PRIx32 "\n", mbc->cmd_count);
#endif
			uint8_t v;
			if (mbc->cmd_count < mbc->data_size)
				v = mbc->data[mbc->cmd_count];
			else
				v = 0;
			if (++mbc->cmd_count == 0x200)
				end_cmd(mbc);
			return v;
		}
		case MBC_CMD_ROMID1:
		{
#if 0
			printf("romid1 0x%" PRIx32 "\n", mbc->cmd_count);
#endif
			uint8_t v = mbc->chipid[mbc->cmd_count];
			if (mbc->cmd_count == 3)
				end_cmd(mbc);
			else
				mbc->cmd_count++;
			return v;
		}
		case MBC_CMD_ROMID2:
		{
#if 0
			printf("romid2 0x%" PRIx32 "\n", mbc->cmd_count);
#endif
			uint8_t v = mbc->chipid[mbc->cmd_count];
			if (mbc->cmd_count == 3)
				end_cmd(mbc);
			else
				mbc->cmd_count++;
			return key2_byte(mbc, v);
		}
		case MBC_CMD_SECBLK:
		{
#if 0
			printf("secblk 0x%" PRIx32 "\n", mbc->cmd_count);
#endif
			uint8_t v;
			uint32_t off = mbc->cmd_count + mbc->cmd_off;
			if (off >= 0x4000 && off < 0x4800)
			{
#if 0
				printf("secure area %08" PRIx32 "\n", off - 0x4000);
#endif
				v = mbc->secure_area[off - 0x4000];
			}
			else if (off < mbc->data_size)
			{
#if 0
				printf("regular data %08" PRIx32 "\n", off);
#endif
				v = mbc->data[off];
			}
			else
			{
				printf("secblk read too far!\n");
				v = 0;
			}
			mbc->cmd_count++;
			if (mbc->cmd_count == 0x1000)
				end_cmd(mbc);
			return key2_byte(mbc, v);
		}
		case MBC_CMD_ENCREAD:
		{
#if 0
			printf("encread 0x%" PRIx32 "\n", mbc->cmd_count);
#endif
			uint8_t v;
			uint32_t off = mbc->cmd_off + mbc->cmd_count;
			off %= mbc->data_size;
			if (off < 0x8000)
				off = 0x8000 + (off & 0x1FF);
			v = mbc->data[off];
#if 0
			printf("mbc read [%08" PRIx32 "] = %02" PRIx8 "\n", off, v);
#endif
			mbc->cmd_count++;
			if (mbc->cmd_count == 0x200)
				end_cmd(mbc);
			return key2_byte(mbc, v);
		}
		default:
			assert(!"unknown cmd");
	}
}

void mbc_write(mbc_t *mbc, uint8_t v)
{
	(void)v;
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

uint8_t mbc_spi_read(mbc_t *mbc)
{
#if 1
	printf("MBC SPI read 0x%02" PRIx8 "\n", mbc->spi.read_latch);
#endif
	return mbc->spi.read_latch;
}

void mbc_spi_write(mbc_t *mbc, uint8_t v)
{
#if 1
	printf("MBC SPI write %02" PRIx8 "\n", v);
#endif
	switch (mbc->spi.cmd)
	{
		case MBC_SPI_CMD_NONE:
			mbc->spi.cmd = v;
			return;
		case MBC_SPI_CMD_RDSR:
			mbc->spi.read_latch = (mbc->spi.write & 1) << 1;
			return;
		case MBC_SPI_CMD_WREN:
			mbc->spi.write = 1;
			return;
		case MBC_SPI_CMD_WRDI:
			mbc->spi.write = 0;
			return;
		default:
			printf("unknown MBC SPI cmd: 0x%02" PRIx8 "\n",
			       mbc->spi.cmd);
			return;
	}
}

void mbc_spi_reset(mbc_t *mbc)
{
	mbc->spi.cmd = MBC_SPI_CMD_NONE;
}
