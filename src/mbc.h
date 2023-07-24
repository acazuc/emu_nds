#ifndef MBC_H
#define MBC_H

#include <stdint.h>
#include <stddef.h>

typedef struct nds nds_t;

enum mbc_cmd
{
	MBC_CMD_NONE,
	MBC_CMD_DUMMY,
	MBC_CMD_GETHDR,
	MBC_CMD_ROMID1,
	MBC_CMD_ROMID2,
	MBC_CMD_SECBLK,
	MBC_CMD_ENCREAD,
};

typedef struct mbc
{
	nds_t *nds;
	uint8_t *data;
	size_t data_size;
	enum mbc_cmd cmd;
	uint8_t enc;
	uint32_t keybuf[0x412];
	uint64_t key2_x;
	uint64_t key2_y;
	uint32_t cmd_count;
	uint32_t cmd_off;
	uint8_t secure_area[0x800];
} mbc_t;

mbc_t *mbc_new(nds_t *nds, const void *data, size_t size);
void mbc_del(mbc_t *mbc);

void mbc_cmd(mbc_t *mbc);
uint8_t mbc_read(mbc_t *mbc);
void mbc_write(mbc_t *mbc, uint8_t v);

#endif
