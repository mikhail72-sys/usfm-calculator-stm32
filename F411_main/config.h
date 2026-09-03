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

/* ---- Firmware identity (usfmInfo of the calculator, F17 on addr 2) ---- */
#define CALC_FW_VER         0x0010  /* BCD 0.10: stage 1, transit + simulator */

/* ---- USB identity ---- */
#define USB_VID             0x0483  /* STMicroelectronics */
#define USB_PID             0x5740  /* stock ST "Virtual COM Port" PID: binds
                                     * usbser.sys on Win10+ with no .inf */
#define USB_BCD_DEVICE      0x0010

#define USB_STR_MANUF       "Piezus"
#define USB_STR_PRODUCT     "USFM calc F411"
#define USB_STR_SERIAL      "USFMCALC-0001"

/* ---- USB endpoints (OTG FS has EP0..EP3 in each direction) ---- */
#define EP_COMM             0x83    /* interrupt IN  (CDC notifications) */
#define EP_DATA_OUT         0x01    /* bulk OUT (host -> device)         */
#define EP_DATA_IN          0x82    /* bulk IN  (device -> host)         */

#define USB_BULK_MPS        64
#define USB_INT_MPS         16

/* ---- Ring buffers (power of two) ----
 * TX must hold several stream records (744 B for a v1 frame) so a burst
 * of frames does not drop while the host is between reads. */
#define VCP_TX_BUF          8192    /* device -> host */
#define VCP_RX_BUF          1024    /* host -> device */

/* ---- Default line coding reported to the host (informational only) ---- */
#define LINE_DEFAULT_BAUD   115200
#define LINE_DEFAULT_BITS   8

/* ---- LED ---- */
#define LED_ACTIVE_LOW      1       /* black pill PC13: LED lit when pin is low */

/* ---- I2C link to the MSP430 ---- */
#define I2C_KHZ_DEFAULT     400     /* 4.7k pull-ups on the MSP430 board; verified
                                     * 2026-09-03.  100 kHz reads a 1012 B frame in
                                     * ~100 ms and the slave overwrites its RAW
                                     * buffers under us at 10 fps (torn frames) */
#define I2C_REPLY_DELAY_MS  2       /* write STOP -> first read: the slave parses
                                     * the request in its main loop; a read within
                                     * ~1 ms gets the previous reply (fw 1.60) */
#define I2C_TMO_WRITE_MS    50      /* request frame (<= 68 bytes) */
#define I2C_TMO_LEN_MS      200     /* length prefix: slave stretches SCL while
                                     * it builds the reply (~5 ms on F103) */
#define I2C_TMO_BODY_MS     500     /* 738 B body: ~66 ms at 100 kHz */
#define I2C_ERR_BACKOFF_MS  100     /* after a failed exchange leave the bus alone */

/* ---- Simulator defaults ---- */
#define SIM_RATE_DEFAULT    10      /* frames per second when source = SIM */
#define SIM_SAMPLES         176     /* v1 frame: 176 samples per direction */

#endif  /* _CONFIG_H_INCLUDED_ */
