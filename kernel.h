#ifndef _KERNEL_H_INCLUDED_
#define _KERNEL_H_INCLUDED_

/* kernel.h -- the MCU-neutral face of the hardware layer.
 *
 * Implemented per target in <TARGET>_main/kernel_<mcu>.c.  Shared code
 * (usfmcalc.c, usb_vcp.c, usfm_link.c, ...) must not touch registers or
 * libopencm3 chip headers directly -- add a call here instead.
 */

#include <stdint.h>
#include <libopencm3/usb/usbd.h>

/* Clocks, GPIO, SysTick.  Call once, first thing in main(). */
void KernelInit(void);

/* Free-running millisecond counter (SysTick), wraps after ~49 days. */
uint32_t KernelTickMs(void);

/* On-board LED and user key */
void LedSet(int on);
void LedToggle(void);
int  KeyPressed(void);

/* USB device controller of this target (libopencm3 driver object) and the
 * per-chip fix-up applied right after usbd_init() -- see the F411 version
 * for why it exists (VBUS sensing). */
const usbd_driver *KernelUsbDriver(void);
void KernelUsbPostInit(void);

/* ---- I2C master towards the MSP430 (blocking, polled, with timeouts) ----
 * One bus, 7-bit addressing, the slave may stretch SCL for milliseconds
 * (it builds the reply while we wait) -- every wait is bounded by
 * timeout_ms and a timeout resets the peripheral and frees the bus. */
#define I2C_OK              0
#define I2C_ERR_TIMEOUT     1   /* no SB/ADDR/BTF/RxNE in time (stretching too long, bus dead) */
#define I2C_ERR_NACK        2   /* address or data not acknowledged */
#define I2C_ERR_BUS         3   /* bus error / arbitration lost */

void I2cInit(uint32_t khz);                     /* 100 or 400; re-callable */
int  I2cWrite(uint8_t addr, const uint8_t *buf, int len, uint32_t timeout_ms);
int  I2cRead(uint8_t addr, uint8_t *buf, int len, uint32_t timeout_ms);
int  I2cRecover(void);                          /* 9 SCL pulses + STOP, re-init; 0 = SDA released */

/* DRDY line from the MSP430: 1 = an unread frame is waiting. */
int  DrdyActive(void);

#endif  /* _KERNEL_H_INCLUDED_ */
