#ifndef MEM_H
#define MEM_H

#include <stdint.h>
#include <stddef.h>

#define MEM_ARM9_REG_DISPCNTA      0x000
#define MEM_ARM9_REG_DISPSTAT      0x004
#define MEM_ARM9_REG_VCOUNT        0x006
#define MEM_ARM9_REG_BG0CNT        0x008
#define MEM_ARM9_REG_BG1CNT        0x00A
#define MEM_ARM9_REG_BG2CNT        0x00C
#define MEM_ARM9_REG_BG3CNT        0x00E
/* XXX gpu */
#define MEM_ARM9_REG_DISP3DCNT     0x060
#define MEM_ARM9_REG_DISPCAPCNT    0x064
#define MEM_ARM9_REG_DISPMFIFO     0x068
#define MEM_ARM9_REG_MASTER_BRIGHT 0x06C

#define MEM_ARM9_REG_DMA0SAD       0x0B0
#define MEM_ARM9_REG_DMA0DAD       0x0B4
#define MEM_ARM9_REG_DMA0CNT_L     0x0B8
#define MEM_ARM9_REG_DMA0CNT_H     0x0BA
#define MEM_ARM9_REG_DMA1SAD       0x0BC
#define MEM_ARM9_REG_DMA1DAD       0x0C0
#define MEM_ARM9_REG_DMA1CNT_L     0x0C4
#define MEM_ARM9_REG_DMA1CNT_H     0x0C6
#define MEM_ARM9_REG_DMA2SAD       0x0C8
#define MEM_ARM9_REG_DMA2DAD       0x0CC
#define MEM_ARM9_REG_DMA2CNT_L     0x0D0
#define MEM_ARM9_REG_DMA2CNT_H     0x0D2
#define MEM_ARM9_REG_DMA3SAD       0x0D4
#define MEM_ARM9_REG_DMA3DAD       0x0D8
#define MEM_ARM9_REG_DMA3CNT_L     0x0DC
#define MEM_ARM9_REG_DMA3CNT_H     0x0DE
#define MEM_ARM9_REG_DMA0FILL      0x0E0
#define MEM_ARM9_REG_DMA1FILL      0x0E4
#define MEM_ARM9_REG_DMA2FILL      0x0E8
#define MEM_ARM9_REG_DMA3FILL      0x0EC

#define MEM_ARM9_REG_TM0CNT_L      0x100
#define MEM_ARM9_REG_TM0CNT_H      0x102
#define MEM_ARM9_REG_TM1CNT_L      0x104
#define MEM_ARM9_REG_TM1CNT_H      0x106
#define MEM_ARM9_REG_TM2CNT_L      0x108
#define MEM_ARM9_REG_TM2CNT_H      0x10A
#define MEM_ARM9_REG_TM3CNT_L      0x10C
#define MEM_ARM9_REG_TM3CNT_H      0x10E

#define MEM_ARM9_REG_KEYINPUT      0x130
#define MEM_ARM9_REG_KEYCNT        0x132

#define MEM_ARM9_REG_IPCSYNC       0x180
#define MEM_ARM9_REG_IPCFIFOCNT    0x184
#define MEM_ARM9_REG_IPCFIFOSEND   0x188

#define MEM_ARM9_REG_AUXSPICNT     0x1A0
#define MEM_ARM9_REG_AUXSPIDATA    0x1A2
#define MEM_ARM9_REG_ROMCTRL       0x1A4
#define MEM_ARM9_REG_ROMCMD        0x1A8
#define MEM_ARM9_REG_ROMSEED0_L    0x1B0
#define MEM_ARM9_REG_ROMSEED1_L    0x1B4
#define MEM_ARM9_REG_ROMSEED0_H    0x1B8
#define MEM_ARM9_REG_ROMSEED1_H    0x1BA

#define MEM_ARM9_REG_EXMEMCNT      0x204
#define MEM_ARM9_REG_IME           0x208
#define MEM_ARM9_REG_IE            0x210
#define MEM_ARM9_REG_IF            0x214

#define MEM_ARM9_REG_VRAMCNT_A     0x240
#define MEM_ARM9_REG_VRAMCNT_B     0x241
#define MEM_ARM9_REG_VRAMCNT_C     0x242
#define MEM_ARM9_REG_VRAMCNT_D     0x243
#define MEM_ARM9_REG_VRAMCNT_E     0x244
#define MEM_ARM9_REG_VRAMCNT_F     0x245
#define MEM_ARM9_REG_VRAMCNT_G     0x246
#define MEM_ARM9_REG_WRAMCNT       0x247
#define MEM_ARM9_REG_VRAMCNT_H     0x248
#define MEM_ARM9_REG_VRAMCNT_I     0x249

#define MEM_ARM9_REG_DIVCNT        0x280
#define MEM_ARM9_REG_DIV_NUMER     0x290
#define MEM_ARM9_REG_DIV_DENOM     0x298
#define MEM_ARM9_REG_DIV_RESULT    0x2A0
#define MEM_ARM9_REG_DIVREM_RESULT 0x2A8
#define MEM_ARM9_REG_SQRTCNT       0x2B0
#define MEM_ARM9_REG_SQRT_RESULT   0x2B4
#define MEM_ARM9_REG_SQRT_PARAM    0x2B8

#define MEM_ARM9_REG_POSTFLG       0x300
#define MEM_ARM9_REG_POWCNT1       0x304

#define MEM_ARM9_REG_IPCFIFORECV   0x100000
#define MEM_ARM9_REG_ROMDATA       0x100010


#define MEM_ARM7_REG_DISPSTAT      0x004
#define MEM_ARM7_REG_VCOUNT        0x006

#define MEM_ARM7_REG_DMA0SAD       0x0B0
#define MEM_ARM7_REG_DMA0DAD       0x0B4
#define MEM_ARM7_REG_DMA0CNT_L     0x0B8
#define MEM_ARM7_REG_DMA0CNT_H     0x0BA
#define MEM_ARM7_REG_DMA1SAD       0x0BC
#define MEM_ARM7_REG_DMA1DAD       0x0C0
#define MEM_ARM7_REG_DMA1CNT_L     0x0C4
#define MEM_ARM7_REG_DMA1CNT_H     0x0C6
#define MEM_ARM7_REG_DMA2SAD       0x0C8
#define MEM_ARM7_REG_DMA2DAD       0x0CC
#define MEM_ARM7_REG_DMA2CNT_L     0x0D0
#define MEM_ARM7_REG_DMA2CNT_H     0x0D2
#define MEM_ARM7_REG_DMA3SAD       0x0D4
#define MEM_ARM7_REG_DMA3DAD       0x0D8
#define MEM_ARM7_REG_DMA3CNT_L     0x0DC
#define MEM_ARM7_REG_DMA3CNT_H     0x0DE

#define MEM_ARM7_REG_TM0CNT_L      0x100
#define MEM_ARM7_REG_TM0CNT_H      0x102
#define MEM_ARM7_REG_TM1CNT_L      0x104
#define MEM_ARM7_REG_TM1CNT_H      0x106
#define MEM_ARM7_REG_TM2CNT_L      0x108
#define MEM_ARM7_REG_TM2CNT_H      0x10A
#define MEM_ARM7_REG_TM3CNT_L      0x10C
#define MEM_ARM7_REG_TM3CNT_H      0x10E

#define MEM_ARM7_REG_SIODATA32     0x120
#define MEM_ARM7_REG_SIOCNT        0x128

#define MEM_ARM7_REG_KEYINPUT      0x130
#define MEM_ARM7_REG_KEYCNT        0x132
#define MEM_ARM7_REG_RCNT          0x134
#define MEM_ARM7_REG_EXTKEYIN      0x136
#define MEM_ARM7_REG_RTC           0x138

#define MEM_ARM7_REG_IPCSYNC       0x180
#define MEM_ARM7_REG_IPCFIFOCNT    0x184
#define MEM_ARM7_REG_IPCFIFOSEND   0x188

#define MEM_ARM7_REG_AUXSPICNT     0x1A0
#define MEM_ARM7_REG_AUXSPIDATA    0x1A2
#define MEM_ARM7_REG_ROMCTRL       0x1A4
#define MEM_ARM7_REG_ROMCMD        0x1A8
#define MEM_ARM7_REG_ROMSEED0_L    0x1B0
#define MEM_ARM7_REG_ROMSEED1_L    0x1B4
#define MEM_ARM7_REG_ROMSEED0_H    0x1B8
#define MEM_ARM7_REG_ROMSEED1_H    0x1BA
#define MEM_ARM7_REG_SPICNT        0x1C0
#define MEM_ARM7_REG_SPIDATA       0x1C2

#define MEM_ARM7_REG_EXMEMSTAT     0x204
#define MEM_ARM7_REG_WIFIWAITCNT   0x206
#define MEM_ARM7_REG_IME           0x208
#define MEM_ARM7_REG_IE            0x210
#define MEM_ARM7_REG_IF            0x214

#define MEM_ARM7_REG_VRAMSTAT      0x240
#define MEM_ARM7_REG_WRAMSTAT      0x241

#define MEM_ARM7_REG_POSTFLG       0x300
#define MEM_ARM7_REG_HALTCNT       0x301
#define MEM_ARM7_REG_POWCNT2       0x304
#define MEM_ARM7_REG_BIOSPROT      0x308

#define MEM_ARM7_REG_SOUNDCNT      0x500
#define MEM_ARM7_REG_SOUNDBIAS     0x504
#define MEM_ARM7_REG_SNDCAP0CNT    0x508
#define MEM_ARM7_REG_SNDCAP1CNT    0x509
#define MEM_ARM7_REG_SNDCAP0DAD    0x510
#define MEM_ARM7_REG_SNDCAP0LEN    0x514
#define MEM_ARM7_REG_SNDCAP1DAD    0x518
#define MEM_ARM7_REG_SNDCAP1LEN    0x51C

#define MEM_ARM7_REG_IPCFIFORECV   0x100000
#define MEM_ARM7_REG_ROMDATA       0x100010

typedef struct mbc mbc_t;
typedef struct nds nds_t;

enum mem_type
{
	MEM_DIRECT,
	MEM_DATA_SEQ,
	MEM_DATA_NSEQ,
	MEM_CODE_SEQ,
	MEM_CODE_NSEQ,
};

struct timer
{
	uint16_t v;
};

#define MEM_DMA_ACTIVE (1 << 0)
#define MEM_DMA_ENABLE (1 << 1)

struct dma
{
	uint8_t status;
	uint32_t src;
	uint32_t dst;
	uint32_t len;
	uint32_t cnt;
};

struct fifo
{
	uint8_t data[64];
	uint8_t len;
	uint8_t pos;
	uint8_t latch[4];
};

struct spi_firmware
{
	uint8_t cmd;
	uint8_t read_latch;
	union
	{
		struct
		{
			uint8_t posb;
			uint32_t addr;
		} read;
	} cmd_data;
};

struct rtc
{
	int cmd_flip;
	uint8_t cmd;
	uint8_t inbuf;
	uint8_t inlen; /* in bits */
	uint8_t outbuf[8];
	uint8_t outlen; /* in bits */
	uint8_t outpos; /* in bits */
	uint8_t outbyte;
	uint8_t wpos;
	uint8_t sr1;
	uint8_t sr2;
	uint8_t fr;
	uint8_t car;
	uint8_t int1_steady_freq;
	uint8_t alarm1[3];
	uint8_t alarm2[3];
};

#define MEM_VRAM_A_OFF 0x00000
#define MEM_VRAM_B_OFF 0x20000
#define MEM_VRAM_C_OFF 0x40000
#define MEM_VRAM_D_OFF 0x60000
#define MEM_VRAM_E_OFF 0x80000
#define MEM_VRAM_F_OFF 0x90000
#define MEM_VRAM_G_OFF 0x94000
#define MEM_VRAM_H_OFF 0x98000
#define MEM_VRAM_I_OFF 0xA0000

typedef struct mem
{
	nds_t *nds;
	mbc_t *mbc;
	struct timer arm7_timers[4];
	struct timer arm9_timers[4];
	struct dma arm7_dma[4];
	struct dma arm9_dma[4];
	struct fifo arm7_fifo;
	struct fifo arm9_fifo;
	struct spi_firmware spi_firmware;
	struct rtc rtc;
	uint8_t arm7_bios[0x4000];
	uint8_t arm9_bios[0x1000];
	uint8_t firmware[0x40000];
	uint8_t arm7_regs[0x600];
	uint8_t arm9_regs[0x700];
	uint8_t mram[0x400000];
	uint8_t wram[0x8000];
	uint8_t arm7_wram[0x10000];
	uint32_t arm7_wram_base;
	uint32_t arm7_wram_mask;
	uint32_t arm9_wram_base;
	uint32_t arm9_wram_mask;
	uint8_t dtcm[0x4000];
	uint8_t itcm[0x8000];
	uint8_t vram[0xA4000];
	uint8_t oam[0x800];
	uint8_t palettes[0x800];
	int biosprot;
	uint8_t *vram_lcdc_a;
	uint8_t *vram_lcdc_b;
	uint8_t *vram_lcdc_c;
	uint8_t *vram_lcdc_d;
	uint8_t *vram_lcdc_e;
	uint8_t *vram_lcdc_f;
	uint8_t *vram_lcdc_g;
	uint8_t *vram_lcdc_h;
	uint8_t *vram_lcdc_i;
} mem_t;

mem_t *mem_new(nds_t *nds, mbc_t *mbc);
void mem_del(mem_t *mem);

void mem_timers(mem_t *mem);
uint8_t mem_dma(mem_t *mem);
void mem_vblank(mem_t *mem);
void mem_hblank(mem_t *mem);
void mem_dscard(mem_t *mem);

uint8_t  mem_arm7_get8 (mem_t *mem, uint32_t addr, enum mem_type type);
uint16_t mem_arm7_get16(mem_t *mem, uint32_t addr, enum mem_type type);
uint32_t mem_arm7_get32(mem_t *mem, uint32_t addr, enum mem_type type);
void mem_arm7_set8 (mem_t *mem, uint32_t addr, uint8_t val, enum mem_type type);
void mem_arm7_set16(mem_t *mem, uint32_t addr, uint16_t val, enum mem_type type);
void mem_arm7_set32(mem_t *mem, uint32_t addr, uint32_t val, enum mem_type type);

uint8_t  mem_arm9_get8 (mem_t *mem, uint32_t addr, enum mem_type type);
uint16_t mem_arm9_get16(mem_t *mem, uint32_t addr, enum mem_type type);
uint32_t mem_arm9_get32(mem_t *mem, uint32_t addr, enum mem_type type);
void mem_arm9_set8 (mem_t *mem, uint32_t addr, uint8_t val, enum mem_type type);
void mem_arm9_set16(mem_t *mem, uint32_t addr, uint16_t val, enum mem_type type);
void mem_arm9_set32(mem_t *mem, uint32_t addr, uint32_t val, enum mem_type type);

static inline uint8_t mem_arm9_get_reg8(mem_t *mem, uint32_t reg)
{
	return mem->arm9_regs[reg];
}

static inline void mem_arm9_set_reg8(mem_t *mem, uint32_t reg, uint8_t val)
{
	mem->arm9_regs[reg] = val;
}

static inline uint16_t mem_arm9_get_reg16(mem_t *mem, uint32_t reg)
{
	return *(uint16_t*)&mem->arm9_regs[reg];
}

static inline void mem_arm9_set_reg16(mem_t *mem, uint32_t reg, uint16_t val)
{
	*(uint16_t*)&mem->arm9_regs[reg] = val;
}

static inline uint32_t mem_arm9_get_reg32(mem_t *mem, uint32_t reg)
{
	return *(uint32_t*)&mem->arm9_regs[reg];
}

static inline void mem_arm9_set_reg32(mem_t *mem, uint32_t reg, uint32_t val)
{
	*(uint32_t*)&mem->arm9_regs[reg] = val;
}

static inline uint64_t mem_arm9_get_reg64(mem_t *mem, uint32_t reg)
{
	return *(uint64_t*)&mem->arm9_regs[reg];
}

static inline void mem_arm9_set_reg64(mem_t *mem, uint32_t reg, uint64_t val)
{
	*(uint64_t*)&mem->arm9_regs[reg] = val;
}

static inline uint8_t mem_arm7_get_reg8(mem_t *mem, uint32_t reg)
{
	return mem->arm7_regs[reg];
}

static inline void mem_arm7_set_reg8(mem_t *mem, uint32_t reg, uint8_t val)
{
	mem->arm7_regs[reg] = val;
}

static inline uint16_t mem_arm7_get_reg16(mem_t *mem, uint32_t reg)
{
	return *(uint16_t*)&mem->arm7_regs[reg];
}

static inline void mem_arm7_set_reg16(mem_t *mem, uint32_t reg, uint16_t val)
{
	*(uint16_t*)&mem->arm7_regs[reg] = val;
}

static inline uint32_t mem_arm7_get_reg32(mem_t *mem, uint32_t reg)
{
	return *(uint32_t*)&mem->arm7_regs[reg];
}

static inline void mem_arm7_set_reg32(mem_t *mem, uint32_t reg, uint32_t val)
{
	*(uint32_t*)&mem->arm7_regs[reg] = val;
}

static inline void mem_arm9_if(mem_t *mem, uint32_t f)
{
	mem_arm9_set_reg32(mem, MEM_ARM9_REG_IF, mem_arm9_get_reg32(mem, MEM_ARM9_REG_IF) | f);
}

static inline void mem_arm7_if(mem_t *mem, uint32_t f)
{
	mem_arm7_set_reg32(mem, MEM_ARM7_REG_IF, mem_arm7_get_reg32(mem, MEM_ARM7_REG_IF) | f);
}

#endif
