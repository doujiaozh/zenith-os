/* ZenithOS v6.0 - minimal memory/string helpers (freestanding). */
#ifndef ZENITH_STRING_H
#define ZENITH_STRING_H

#include <stddef.h>
#include <stdint.h>

void   *kmemset(void *dst, int value, size_t n);
void   *kmemcpy(void *dst, const void *src, size_t n);
void   *kmemmove(void *dst, const void *src, size_t n);
int     kmemcmp(const void *a, const void *b, size_t n);
size_t  kstrlen(const char *s);
int     kstrcmp(const char *a, const char *b);
int     kstrncmp(const char *a, const char *b, size_t n);
char   *kstrcpy(char *dst, const char *src);
char   *kstrncpy(char *dst, const char *src, size_t n);

#endif /* ZENITH_STRING_H */
