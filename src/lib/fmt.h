/* ZenithOS v6.0 - minimal printf-compatible formatter. */
#ifndef ZENITH_FMT_H
#define ZENITH_FMT_H

typedef void (*char_sink_t)(char);

/* Register an extra sink (the GUI terminal). serial is always used. */
void fmt_set_sink(char_sink_t sink);

void kprintf(const char *fmt, ...);
void klog(const char *fmt, ...);   /* kprintf + '\n' */

#endif /* ZENITH_FMT_H */
