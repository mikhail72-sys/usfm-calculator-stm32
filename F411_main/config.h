#ifndef _CONFIG_H_INCLUDED_
#define _CONFIG_H_INCLUDED_

/* config.h -- F411_main project settings (WeAct black pill, STM32F411CEU6)
 *
 * Only things that may differ between device variants live here; anything
 * shared between targets belongs in the project root.
 */

/* ---- Build variant ---- */
#define CPU_MHZ             96      /* AHB; PLL 25 MHz HSE -> 96 MHz, PLLQ -> 48 MHz USB */
#define TICK_HZ             1000    /* SysTick period, ms tick for KernelTickMs() */

/* ---- USB identity ---- */
#define USB_VID             0x0483  /* STMicroelectronics */
#define USB_PID             0x5740  /* stock ST "Virtual COM Port" PID: binds
                                     * usbser.sys on Win10+ with no .inf */
#define USB_BCD_DEVICE      0x0000  /* v0 */

#define USB_STR_MANUF       "Piezus"
#define USB_STR_PRODUCT     "USFM calc F411 (v0 echo)"
#define USB_STR_SERIAL      "USFMCALC-0001"

/* ---- USB endpoints (OTG FS has EP0..EP3 in each direction) ---- */
#define EP_COMM             0x83    /* interrupt IN  (CDC notifications) */
#define EP_DATA_OUT         0x01    /* bulk OUT (host -> device)         */
#define EP_DATA_IN          0x82    /* bulk IN  (device -> host)         */

#define USB_BULK_MPS        64
#define USB_INT_MPS         16

/* ---- Ring buffers (power of two) ---- */
#define VCP_TX_BUF          1024    /* device -> host */
#define VCP_RX_BUF          1024    /* host -> device */

/* ---- Default line coding reported to the host (informational only:
 * nothing physical hangs off this VCP yet) ---- */
#define LINE_DEFAULT_BAUD   115200
#define LINE_DEFAULT_BITS   8

/* ---- LED ---- */
#define LED_ACTIVE_LOW      1       /* black pill PC13: LED lit when pin is low */

#endif  /* _CONFIG_H_INCLUDED_ */
