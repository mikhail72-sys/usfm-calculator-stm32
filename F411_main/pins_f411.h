#ifndef _PINS_H_INCLUDED_
#define _PINS_H_INCLUDED_

#include "config.h"

/* WeAct STM32F411CEU6 "black pill" v2/v3
 * HSE 25 MHz, LSE 32.768 kHz, LED on PC13 (active low), KEY on PA0
 * (active low, external pull-up), USB-C on PA11/PA12 (OTG FS, AF10),
 * no VBUS wire to PA9 -- see KernelUsbPostInit().
 */

// PORTA
#define KEY_Pin             GPIO0
#define USB_DM_Pin          GPIO11
#define USB_DP_Pin          GPIO12

// PORTC
#define LED_Port            GPIOC
#define LED_RCC             RCC_GPIOC
#define LED_Pin             GPIO13

#endif // _PINS_H_INCLUDED_
