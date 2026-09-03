/* usfmcalc.c -- application entry, shared by every MCU target.
 *
 * v0: empty application + USB CDC echo port for bench testing.
 *
 *   - everything the host sends to the virtual COM port is sent back
 *     byte for byte (binary-safe, no CR/LF massaging);
 *   - a banner is printed each time the host opens the port (DTR rises);
 *   - the on-board LED tells the USB state:
 *       slow blink 1 Hz        not enumerated / not configured
 *       short blip every 1 s   configured, port closed
 *       solid on               port open (DTR), flickers off on traffic
 *   - the user key, while held, is reported as "KEY\r\n" once per press
 *     (only when the port is open).
 */
#include <stdint.h>
#include <stdbool.h>

#include "config.h"
#include "kernel.h"
#include "usb_vcp.h"

#define BANNER      "\r\nusfmcalc v0 (" USB_STR_PRODUCT ") -- echo port ready\r\n"
#define FLICKER_MS  30

static void led_task(uint32_t now, uint32_t last_traffic)
{
    uint32_t phase = now % 1000;

    if (!usb_vcp_configured()) {
        LedSet(phase < 500);
    } else if (!usb_vcp_dtr()) {
        LedSet(phase < 50);
    } else {
        LedSet((uint32_t)(now - last_traffic) > FLICKER_MS);
    }
}

int main(void)
{
    uint8_t  buf[64];
    bool     dtr_was = false, key_was = false;
    uint32_t last_traffic = 0;

    KernelInit();
    usb_vcp_init();

    for (;;) {
        usb_vcp_poll();

        uint32_t now = KernelTickMs();
        bool dtr = usb_vcp_dtr();

        if (dtr && !dtr_was)
            usb_vcp_puts(BANNER);
        dtr_was = dtr;

        /* echo: move as much as the TX ring will take, leave the rest in RX
         * (the USB layer NAKs the host while RX is full -- nothing is lost) */
        int room = usb_vcp_tx_free();
        if (room > (int)sizeof(buf)) room = sizeof(buf);
        if (room > 0) {
            int n = usb_vcp_read(buf, room);
            if (n > 0) {
                usb_vcp_write(buf, n);
                last_traffic = now;
            }
        }

        bool key = KeyPressed();
        if (key && !key_was && dtr)
            usb_vcp_puts("KEY\r\n");
        key_was = key;

        led_task(now, last_traffic);
    }
}
