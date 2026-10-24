/* ZenithOS v6.0 - panic / fault reporting. */
#ifndef ZENITH_PANIC_H
#define ZENITH_PANIC_H

#include <stdint.h>

void panic(const char *msg);
void panic_exception(uint8_t int_no, uint32_t err, uint32_t eip,
                     uint32_t eax, uint32_t ebx);

#endif /* ZENITH_PANIC_H */
