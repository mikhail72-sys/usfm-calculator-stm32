#ifndef _KERNEL_H_INCLUDED_
#define _KERNEL_H_INCLUDED_

/* kernel.h -- the MCU-neutral face of the hardware layer.
 *
 * Implemented per target in <TARGET>_main/kernel_<mcu>.c.  Shared code
 * (usfmcalc.c, usb_vcp.c) must not touch registers or libopencm3 chip
 * headers directly -- add a call here instead.
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

#endif  /* _KERNEL_H_INCLUDED_ */
