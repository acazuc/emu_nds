#ifndef NDS_H
#define NDS_H

#include <stddef.h>
#include <stdint.h>

typedef struct mbc mbc_t;
typedef struct mem mem_t;
typedef struct apu apu_t;
typedef struct cpu cpu_t;
typedef struct gpu gpu_t;

enum nds_button
{
	NDS_BUTTON_RIGHT  = (1 << 0),
	NDS_BUTTON_LEFT   = (1 << 1),
	NDS_BUTTON_UP     = (1 << 2),
	NDS_BUTTON_DOWN   = (1 << 3),
	NDS_BUTTON_A      = (1 << 4),
	NDS_BUTTON_B      = (1 << 5),
	NDS_BUTTON_X      = (1 << 6),
	NDS_BUTTON_Y      = (1 << 7),
	NDS_BUTTON_L      = (1 << 8),
	NDS_BUTTON_R      = (1 << 9),
	NDS_BUTTON_SELECT = (1 << 10),
	NDS_BUTTON_START  = (1 << 11),
};

typedef struct nds
{
	mbc_t *mbc;
	mem_t *mem;
	apu_t *apu;
	cpu_t *arm7;
	cpu_t *arm9;
	gpu_t *gpu;
	uint32_t joypad;
	uint64_t cycle;
} nds_t;

nds_t *nds_new(const void *rom_data, size_t rom_size);
void nds_del(nds_t *nds);

void nds_frame(nds_t *nds, uint8_t *video_buf, int16_t *audio_buf, uint32_t joypad,
               uint8_t touch_x, uint8_t touch_y, uint8_t touch);

void nds_set_arm7_bios(nds_t *nds, const uint8_t *data);
void nds_set_arm9_bios(nds_t *nds, const uint8_t *data);
void nds_set_firmware(nds_t *nds, const uint8_t *data);

void nds_get_mbc_ram(nds_t *nds, uint8_t **data, size_t *size);
void nds_get_mbc_rtc(nds_t *nds, uint8_t **data, size_t *size);

void nds_test_keypad_int(nds_t *nds);

#endif
