/* ZenithOS v6.0 - PIT driver. */
#include "pit.h"
#include "hal/x86/port.h"
#include "hal/x86/cpu.h"
#include "config.h"

static volatile uint32_t ticks = 0;

void pit_init(uint32_t hz) {
    uint32_t div = PIT_BASE / hz;
    outb(0x43, 0x36);              /* channel 0, lobyte/hibyte, square wave */
    outb(0x40, (uint8_t)(div & 0xFF));
    outb(0x40, (uint8_t)((div >> 8) & 0xFF));
}

void pit_tick(void) { ticks++; }
uint32_t pit_ticks(void) { return ticks; }

void pit_sleep_ms(uint32_t ms) {
    uint32_t target = ticks + (ms * PIT_HZ) / 1000u;
    while (ticks < target) cpu_hlt();
}
