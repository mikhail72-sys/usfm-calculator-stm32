/* kernel_f411.c -- hardware layer for the STM32F411 target
 *
 * Clock tree, SysTick ms tick, LED, KEY, USB OTG FS plumbing.  Everything
 * the shared code (usfmcalc.c / usb_vcp.c) needs from the chip goes
 * through kernel.h, so a second MCU target only re-implements this file.
 */
#include <libopencm3/cm3/systick.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/dwc/otg_fs.h>

#include "pins_f411.h"
#include "config.h"
#include "kernel.h"

static volatile uint32_t tick_ms;

void sys_tick_handler(void)
{
    tick_ms++;
}

uint32_t KernelTickMs(void)
{
    return tick_ms;
}

void LedSet(int on)
{
#if LED_ACTIVE_LOW
    if (on) gpio_clear(LED_Port, LED_Pin); else gpio_set(LED_Port, LED_Pin);
#else
    if (on) gpio_set(LED_Port, LED_Pin); else gpio_clear(LED_Port, LED_Pin);
#endif
}

void LedToggle(void)
{
    gpio_toggle(LED_Port, LED_Pin);
}

int KeyPressed(void)
{
    return !gpio_get(GPIOA, KEY_Pin);
}

void KernelInit(void)
{
    /* 25 MHz HSE -> PLL: M=25 N=192 P=2 Q=4 -> SYSCLK 96 MHz, USB 48 MHz,
     * APB1 48 MHz, APB2 96 MHz, 3 WS, VOS scale 1 */
    rcc_clock_setup_pll(&rcc_hse_25mhz_3v3[RCC_CLOCK_3V3_96MHZ]);

    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(LED_RCC);

    LedSet(0);
    gpio_mode_setup(LED_Port, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_Pin);
    gpio_set_output_options(LED_Port, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, LED_Pin);

    gpio_mode_setup(GPIOA, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, KEY_Pin);

    /* USB OTG FS pins: PA11 = DM, PA12 = DP, AF10 */
    gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, USB_DM_Pin | USB_DP_Pin);
    gpio_set_output_options(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_100MHZ, USB_DM_Pin | USB_DP_Pin);
    gpio_set_af(GPIOA, GPIO_AF10, USB_DM_Pin | USB_DP_Pin);

    /* SysTick: AHB clock, TICK_HZ interrupts */
    systick_set_clocksource(STK_CSR_CLKSOURCE_AHB);
    systick_set_reload(rcc_ahb_frequency / TICK_HZ - 1);
    systick_clear();
    systick_interrupt_enable();
    systick_counter_enable();
}

const usbd_driver *KernelUsbDriver(void)
{
    return &otgfs_usb_driver;
}

/* Called by usb_vcp_init() right after usbd_init().
 *
 * libopencm3's OTG init (usb_f107.c) enables B-session VBUS sensing
 * (GCCFG.VBUSBSEN).  On the black pill PA9 is NOT wired to VBUS, so the
 * core never sees a valid session and never pulls DP up -- the host sees
 * nothing at all.  Fix: disable sensing altogether (NOVBUSSENS, bit 21 on
 * the F4 core) and drop the A/B sense enables.  PWRDWN stays set (PHY
 * powered). */
void KernelUsbPostInit(void)
{
    OTG_FS_GCCFG = (OTG_FS_GCCFG & ~(OTG_GCCFG_VBUSBSEN | OTG_GCCFG_VBUSASEN))
                 | OTG_GCCFG_NOVBUSSENS | OTG_GCCFG_PWRDWN;
}
