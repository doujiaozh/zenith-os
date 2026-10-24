/* ZenithOS v6.0 - CPU control primitives (x86 HAL). */
#ifndef ZENITH_CPU_H
#define ZENITH_CPU_H

#include <stdint.h>

void     cpu_cli(void);
void     cpu_sti(void);
void     cpu_hlt(void);
uint64_t cpu_rdtsc(void);
uint32_t cpu_read_cr0(void);
uint32_t cpu_read_cr2(void);
uint32_t cpu_read_cr3(void);
void     cpu_write_cr0(uint32_t value);
void     cpu_write_cr3(uint32_t value);
void     cpu_invlpg(uint32_t addr);
void     cpu_lgdt(const void *gdtr);
void     cpu_lidt(const void *idtr);

#endif /* ZENITH_CPU_H */
