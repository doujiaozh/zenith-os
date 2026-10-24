/*
 * ZenithOS v6.0 - freestanding <stdint.h> (target: TCC i386, 32-bit)
 * Self-contained; no system headers required.
 */
#ifndef ZENITH_STDINT_H
#define ZENITH_STDINT_H

typedef signed char        int8_t;
typedef unsigned char      uint8_t;
typedef signed short       int16_t;
typedef unsigned short     uint16_t;
typedef signed int         int32_t;
typedef unsigned int       uint32_t;
typedef signed long long   int64_t;
typedef unsigned long long uint64_t;

typedef signed char        int_least8_t;
typedef unsigned char      uint_least8_t;
typedef signed short       int_least16_t;
typedef unsigned short     uint_least16_t;
typedef signed int         int_least32_t;
typedef unsigned int       uint_least32_t;
typedef signed long long   int_least64_t;
typedef unsigned long long uint_least64_t;

typedef signed char        int_fast8_t;
typedef unsigned char      uint_fast8_t;
typedef signed int         int_fast16_t;
typedef unsigned int       uint_fast16_t;
typedef signed int         int_fast32_t;
typedef unsigned int       uint_fast32_t;
typedef signed long long   int_fast64_t;
typedef unsigned long long uint_fast64_t;

typedef signed long long   intmax_t;
typedef unsigned long long uintmax_t;
typedef signed int         intptr_t;
typedef unsigned int       uintptr_t;

#define INT8_MIN   (-128)
#define INT8_MAX   127
#define UINT8_MAX  0xffU
#define INT16_MIN  (-32768)
#define INT16_MAX  32767
#define UINT16_MAX 0xffffU
#define INT32_MIN  (-2147483647 - 1)
#define INT32_MAX  2147483647
#define UINT32_MAX 0xffffffffU
#define INT64_MIN  (-9223372036854775807LL - 1)
#define INT64_MAX  9223372036854775807LL
#define UINT64_MAX 0xffffffffffffffffULL

#define INTMAX_MIN  INT64_MIN
#define INTMAX_MAX  INT64_MAX
#define UINTMAX_MAX UINT64_MAX
#define INTPTR_MIN  INT32_MIN
#define INTPTR_MAX  INT32_MAX
#define UINTPTR_MAX UINT32_MAX
#define SIZE_MAX    UINT32_MAX
#define PTRDIFF_MIN INT32_MIN
#define PTRDIFF_MAX INT32_MAX

#endif /* ZENITH_STDINT_H */
