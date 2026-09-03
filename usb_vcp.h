#ifndef _USB_VCP_H_
#define _USB_VCP_H_

#include <stdint.h>
#include <stdbool.h>

/*
 * Single CDC-ACM virtual COM port over the target's USB device controller.
 *
 * USB is polled: call usb_vcp_poll() from the main loop, often.  All ring
 * access happens in main context (no interrupts involved), so the API is
 * not ISR-safe -- keep it that way until there is a reason not to.
 *
 * Host -> device bytes land in the RX ring; when the ring cannot take a
 * whole packet the OUT endpoint is NAKed (backpressure, nothing is lost)
 * until the application drains it.  Device -> host bytes queue in the TX
 * ring and go out one packet per poll.
 */

/* Decoded line coding from the last CDC SET_LINE_CODING. */
struct vcp_line_params {
    uint32_t baud;
    uint8_t  stop;     /* 1 or 2                 */
    uint8_t  parity;   /* 0=none 1=odd 2=even    */
    uint8_t  bits;     /* data bits (5..8)       */
};

void usb_vcp_init(void);
void usb_vcp_poll(void);
bool usb_vcp_configured(void);   /* host finished SET_CONFIGURATION */
bool usb_vcp_dtr(void);          /* host has the port open (DTR asserted) */

/* device -> host */
int  usb_vcp_write(const uint8_t *data, int len);   /* returns bytes queued */
int  usb_vcp_puts(const char *s);
int  usb_vcp_tx_free(void);

/* host -> device */
int  usb_vcp_read(uint8_t *dst, int max);          /* returns bytes taken */
int  usb_vcp_rx_avail(void);

/* True once per change: the host set a new line coding (fills p). */
bool usb_vcp_line_params(struct vcp_line_params *p);
/* Current line coding without consuming the change flag. */
void usb_vcp_get_line(struct vcp_line_params *p);

#endif /* _USB_VCP_H_ */
