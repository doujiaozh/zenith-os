/* ZenithOS v6.0 - device detection. Decides VGA+keyboard (GUI) vs headless
 * (serial terminal). Runs early, before interrupt controllers and drivers. */
#include "drivers/device.h"
#include "hal/x86/port.h"

static int kbd = 0, vga = 0, srl = 0;
static output_backend_t backend = OUT_SERIAL;

/* 8042 controller self-test: returns 0x55 on a healthy PS/2 controller. */
static int ps2_present(void) {
    uint32_t t;
    for (t = 100000; t && (inb(0x64) & 0x01); t--) (void)inb(0x60); /* flush */
    outb(0x64, 0xAA);                                  /* self-test command   */
    for (t = 100000; t && !(inb(0x64) & 0x01); t--) ;  /* wait for result     */
    if (!t) return 0;
    return inb(0x60) == 0x55;
}

void device_probe(BootInfo *info) {
    vga = (info->video_mode == VIDEO_MODE_13H);
    srl = 1;                     /* COM1 is assumed present; serial_init verifies */
    kbd = ps2_present();
    backend = vga ? OUT_VGA : OUT_SERIAL;
}

output_backend_t device_output(void)  { return backend; }
int device_has_keyboard(void)         { return kbd; }
int device_has_vga(void)              { return vga; }
int device_has_serial(void)           { return srl; }
