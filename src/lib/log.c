/* ZenithOS v6.0 - kernel log ring buffer. */
#include "log.h"
#include "fmt.h"
#include "lib/string.h"

static char ring[LOG_BUF_SIZE];
static unsigned head = 0;   /* write index, wraps */
static unsigned count = 0;  /* total chars (clipped to buffer) */

void log_clear(void) {
    kmemset(ring, 0, sizeof(ring));
    head = 0; count = 0;
}

void log_line(const char *s) {
    const char *p = s;
    while (*s) {
        ring[head % LOG_BUF_SIZE] = *s++;
        head++;
    }
    ring[head % LOG_BUF_SIZE] = '\n'; head++;
    ring[head % LOG_BUF_SIZE] = '\0';
    if (count < LOG_BUF_SIZE) count = head < LOG_BUF_SIZE ? head : LOG_BUF_SIZE;
    klog("%s", p);
}

const char *log_text(void) { return ring; }

unsigned log_len(void) { return count; }
