#ifndef _PINS_H_INCLUDED_
#define _PINS_H_INCLUDED_

#include "config.h"

/* WeAct STM32F411CEU6 "black pill" v2/v3
 * HSE 25 MHz, LSE 32.768 kHz, LED on PC13 (active low), KEY on PA0
 * (active low, external pull-up), USB-C on PA11/PA12 (OTG FS, AF10),
 * no VBUS wire to PA9 -- see KernelUsbPostInit().
 *
 * Link to the MSP430 board (agreed with device 2026-09-03):
 *   SCL  PB6  <->  P1.7      I2C1, AF4, open drain, 4.7k pull-ups on the
 *   SDA  PB7  <->  P1.6      MSP430 board (nothing soldered here)
 *   DRDY PB5  <-   PJ.7      level, 1 = unread frame waiting
 *   GND common; 3V3 NOT connected between boards (each on its own USB)
 */

// PORTA
#define KEY_Pin             GPIO0
#define USB_DM_Pin          GPIO11
#define USB_DP_Pin          GPIO12

// PORTB
#define DRDY_Port           GPIOB
#define DRDY_Pin            GPIO5
#define I2C_Port            GPIOB
#define I2C_SCL_Pin         GPIO6
#define I2C_SDA_Pin         GPIO7

// PORTC
#define LED_Port            GPIOC
#define LED_RCC             RCC_GPIOC
#define LED_Pin             GPIO13

#endif // _PINS_H_INCLUDED_
