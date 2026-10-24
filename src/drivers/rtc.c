/* ZenithOS v6.0 - RTC read via CMOS ports. */
#include "rtc.h"
#include "hal/x86/port.h"

static uint8_t cmos_read(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static uint8_t bcd(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0xF)); }

void rtc_read(rtc_time_t *t) {
    uint8_t s = cmos_read(0x00);
    uint8_t m = cmos_read(0x02);
    uint8_t h = cmos_read(0x04);
    uint8_t b = cmos_read(0x0B);
    if (!(b & 0x04)) {          /* not binary: convert BCD */
        s = bcd(s); m = bcd(m); h = bcd(h);
    }
    t->second = (uint8_t)(s & 0x7F);
    t->minute = (uint8_t)(m & 0x7F);
    t->hour   = (uint8_t)(h & 0x3F);
}
