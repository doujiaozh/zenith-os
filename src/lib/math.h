/* ZenithOS v6.0 - integer/fixed-point math (all in C). */
#ifndef ZENITH_MATH_H
#define ZENITH_MATH_H

#include <stdint.h>

typedef int32_t fixed_t;

#define FIXED_SHIFT 8
#define FIXED_ONE   (1L << FIXED_SHIFT)

int32_t fixed_mul(int32_t a, int32_t b);   /* Q8.8 multiply  */
int32_t fixed_div(int32_t a, int32_t b);   /* Q8.8 divide    */
int32_t isqrt(int32_t n);                  /* integer sqrt   */
int16_t fixed_sin(unsigned angle8);        /* 0..255 = 0..2pi */
int16_t fixed_cos(unsigned angle8);

#endif /* ZENITH_MATH_H */
