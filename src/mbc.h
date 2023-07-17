#ifndef MBC_H
#define MBC_H

#include <stdint.h>
#include <stddef.h>

typedef struct mbc
{
	uint8_t *data;
	size_t data_size;
} mbc_t;

mbc_t *mbc_new(const void *data, size_t size);
void mbc_del(mbc_t *mbc);

#endif
