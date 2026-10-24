/* ZenithOS v6.0 - freestanding <stddef.h> (TCC i386) */
#ifndef ZENITH_STDDEF_H
#define ZENITH_STDDEF_H

typedef __SIZE_TYPE__    size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
typedef __PTRDIFF_TYPE__ ssize_t;
typedef __WCHAR_TYPE__   wchar_t;

#define NULL ((void*)0)
#define offsetof(type, field) ((size_t)&((type *)0)->field)

#endif /* ZENITH_STDDEF_H */
