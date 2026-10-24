/* ZenithOS v6.0 - freestanding <stdarg.h> (TCC, i386 cdecl) */
#ifndef ZENITH_STDARG_H
#define ZENITH_STDARG_H

typedef char *va_list;

/* i386 cdecl: arguments are pushed right-to-left on a uniformly 4-byte
 * aligned stack, so a char* walk is correct and portable across TCC. */
#define va_start(ap, last) ((ap) = (char *)&(last) + ((sizeof(last) + 3) & ~3))
#define va_arg(ap, type)   ((ap) += (sizeof(type) + 3) & ~3, *(type *)((ap) - ((sizeof(type) + 3) & ~3)))
#define va_copy(dest, src) ((dest) = (src))
#define va_end(ap)         ((ap) = (char *)0)

typedef va_list __gnuc_va_list;
#define _VA_LIST_DEFINED

#endif /* ZENITH_STDARG_H */
