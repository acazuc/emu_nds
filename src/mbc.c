#include "mbc.h"
#include "nds.h"
#include "mem.h"

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
	if (cmd == 0x9F00000000000000ULL)
	{
		mbc->cmd = MBC_CMD_DUMMY;
		mbc->cmd_data.dummy.count = 0x2000;
		mbc->nds->mem->arm9_regs[MEM_ARM9_REG_ROMCTRL + 2] |= (1 << 7); /* XXX another way */
		return;
	}
	if (cmd == 0x0000000000000000ULL)
	{
		mbc->cmd = MBC_CMD_GETHDR;
		mbc->cmd_data.gethdr.count = 0;
		mbc->nds->mem->arm9_regs[MEM_ARM9_REG_ROMCTRL + 2] |= (1 << 7); /* XXX another way */
		return;
	}
	if (cmd == 0x9000000000000000ULL)
	{
		mbc->cmd = MBC_CMD_ROMID1;
		mbc->cmd_data.gethdr.count = 0;
		mbc->nds->mem->arm9_regs[MEM_ARM9_REG_ROMCTRL + 2] |= (1 << 7); /* XXX another way */
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
