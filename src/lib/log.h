/* ZenithOS v6.0 - kernel message log (ring buffer, mirrored to console). */
#ifndef ZENITH_LOG_H
#define ZENITH_LOG_H

#define LOG_BUF_SIZE 2048

void        log_clear(void);
void        log_line(const char *s);   /* append to ring + print with newline */
const char *log_text(void);            /* ring contents (best-effort)         */
unsigned    log_len(void);

#endif /* ZENITH_LOG_H */
