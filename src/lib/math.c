/* ZenithOS v6.0 - fixed-point math. */
#include "math.h"
#include "trig.h"

int32_t fixed_mul(int32_t a, int32_t b) {
    return (a * b) >> FIXED_SHIFT;
}

int32_t fixed_div(int32_t a, int32_t b) {
    if (b == 0) return 0;
    return (a << FIXED_SHIFT) / b;
}

int32_t isqrt(int32_t n) {
    int32_t x, y;
    if (n < 0) n = -n;
    if (n < 2) return n;
    x = n; y = (x + 1) / 2;
    while (y < x) { x = y; y = (x + n / x) / 2; }
    return x;
}

int16_t fixed_sin(unsigned a) { return zenith_sin_q8[a & 255]; }
int16_t fixed_cos(unsigned a) { return ZENITH_COS_Q8(a); }
