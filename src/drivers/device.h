/* ZenithOS v6.0 - dynamic device probe / adaptive output selection. */
#ifndef ZENITH_DEVICE_H
#define ZENITH_DEVICE_H

#include "zenith.h"

typedef enum { OUT_SERIAL, OUT_VGA } output_backend_t;

void            device_probe(BootInfo *info);
output_backend_t device_output(void);
int             device_has_keyboard(void);
int             device_has_vga(void);
int             device_has_serial(void);

#endif /* ZENITH_DEVICE_H */
