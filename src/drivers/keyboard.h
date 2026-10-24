/* ZenithOS v6.0 - PS/2 keyboard (set 1 scancodes). */
#ifndef ZENITH_KEYBOARD_H
#define ZENITH_KEYBOARD_H

#include <stdint.h>

void     keyboard_init(void);
void     keyboard_irq(void);    /* ISR hook (IRQ 1) */

int      keyboard_has_char(void);
char     keyboard_get_char(void);
uint32_t keyboard_key_down(uint32_t bit);         /* 1 while held */
int      keyboard_pressed_clear(uint32_t bit);    /* edge latch: 1 once */
void     keyboard_clear_all(void);

#endif /* ZENITH_KEYBOARD_H */
