/* ZenithOS v6.0 - PIT (8253) interval timer. */
#ifndef ZENITH_PIT_H
#define ZENITH_PIT_H

#include <stdint.h>

void     pit_init(uint32_t hz);
void     pit_tick(void);       /* ISR hook (IRQ 0) */
uint32_t pit_ticks(void);      /* monotonic tick count */
void     pit_sleep_ms(uint32_t ms);   /* busy-wait (IRQs must be on) */

#endif /* ZENITH_PIT_H */
