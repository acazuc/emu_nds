#ifndef CPU_INSTR_H
#define CPU_INSTR_H

#include <stdbool.h>
#include <stddef.h>

typedef struct cpu cpu_t;

struct cpu_instr
{
	void (*exec)(cpu_t *cpu);
	void (*print)(cpu_t *cpu, char *data, size_t size);
};

extern const struct cpu_instr *cpu_instr_thumb[0x400];
extern const struct cpu_instr *cpu_instr_arm[0x1000];

#endif
