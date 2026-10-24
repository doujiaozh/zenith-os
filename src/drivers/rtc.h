/* ZenithOS v6.0 - RTC (CMOS) wall clock. */
#ifndef ZENITH_RTC_H
#define ZENITH_RTC_H

#include <stdint.h>

typedef struct { uint8_t hour, minute, second; } rtc_time_t;

void rtc_read(rtc_time_t *t);

#endif /* ZENITH_RTC_H */
