/* kernel_f411.c -- hardware layer for the STM32F411 target
 *
 * Clock tree, SysTick ms tick, LED, KEY, USB OTG FS plumbing, I2C1 master
 * towards the MSP430, DRDY input.  Everything the shared code needs from
 * the chip goes through kernel.h, so a second MCU target only
 * re-implements this file.
 */
#include <libopencm3/cm3/systick.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/i2c.h>
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

int DrdyActive(void)
{
    return gpio_get(DRDY_Port, DRDY_Pin) ? 1 : 0;
}

void KernelInit(void)
{
    /* 25 MHz HSE -> PLL: M=25 N=192 P=2 Q=4 -> SYSCLK 96 MHz, USB 48 MHz,
     * APB1 48 MHz, APB2 96 MHz, 3 WS, VOS scale 1 */
    rcc_clock_setup_pll(&rcc_hse_25mhz_3v3[RCC_CLOCK_3V3_96MHZ]);

    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOB);
    rcc_periph_clock_enable(LED_RCC);

    LedSet(0);
    gpio_mode_setup(LED_Port, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_Pin);
    gpio_set_output_options(LED_Port, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, LED_Pin);

    gpio_mode_setup(GPIOA, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, KEY_Pin);

    /* DRDY: pull-down so an unconnected line reads "nothing waiting" */
    gpio_mode_setup(DRDY_Port, GPIO_MODE_INPUT, GPIO_PUPD_PULLDOWN, DRDY_Pin);

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

/* ====================================================================== *
 *  I2C1 master (F4 "v1" I2C block), polled with timeouts.
 *
 *  Read sequences follow RM0383 "method 2" (N=1, N=2, N>2 special cases:
 *  the last byte must be NACKed before it starts, which on this block
 *  means juggling ACK/POS/STOP around BTF).  The slave (MSP430) stretches
 *  SCL while it prepares the reply -- fine, every wait is bounded by
 *  timeout_ms; on timeout the block is reset so the next call starts
 *  clean.
 * ====================================================================== */
#define BUS     I2C1

static uint32_t i2c_khz_cur = I2C_KHZ_DEFAULT;

static void i2c_setup(void)
{
    i2c_peripheral_disable(BUS);
    I2C_CR1(BUS) |= I2C_CR1_SWRST;
    I2C_CR1(BUS) &= ~I2C_CR1_SWRST;
    i2c_set_speed(BUS, i2c_khz_cur >= 400 ? i2c_speed_fm_400k : i2c_speed_sm_100k,
                  rcc_apb1_frequency / 1000000);
    i2c_peripheral_enable(BUS);
}

void I2cInit(uint32_t khz)
{
    i2c_khz_cur = khz;
    rcc_periph_clock_enable(RCC_I2C1);

    /* AF4 open-drain; internal pull-ups on top of the board's 4.7k do no
     * harm and keep the bus alive on a bare black pill */
    gpio_mode_setup(I2C_Port, GPIO_MODE_AF, GPIO_PUPD_PULLUP, I2C_SCL_Pin | I2C_SDA_Pin);
    gpio_set_output_options(I2C_Port, GPIO_OTYPE_OD, GPIO_OSPEED_25MHZ, I2C_SCL_Pin | I2C_SDA_Pin);
    gpio_set_af(I2C_Port, GPIO_AF4, I2C_SCL_Pin | I2C_SDA_Pin);

    i2c_setup();
}

/* Wait until any bit of `mask` is set in SR1.  Error bits abort early. */
static int i2c_wait(uint32_t mask, uint32_t t0, uint32_t timeout_ms)
{
    for (;;) {
        uint32_t sr1 = I2C_SR1(BUS);
        if (sr1 & mask) return I2C_OK;
        if (sr1 & I2C_SR1_AF) return I2C_ERR_NACK;
        if (sr1 & (I2C_SR1_BERR | I2C_SR1_ARLO)) return I2C_ERR_BUS;
        if ((uint32_t)(KernelTickMs() - t0) > timeout_ms) return I2C_ERR_TIMEOUT;
    }
}

static int i2c_wait_idle(uint32_t t0, uint32_t timeout_ms)
{
    while (I2C_SR2(BUS) & I2C_SR2_BUSY) {
        if ((uint32_t)(KernelTickMs() - t0) > timeout_ms) return I2C_ERR_TIMEOUT;
    }
    return I2C_OK;
}

/* Common failure exit: NACK -> clean STOP; anything else -> block reset. */
static int i2c_fail(int err)
{
    if (err == I2C_ERR_NACK) {
        I2C_SR1(BUS) &= ~I2C_SR1_AF;
        i2c_send_stop(BUS);
        uint32_t t0 = KernelTickMs();
        while ((I2C_CR1(BUS) & I2C_CR1_STOP) && (uint32_t)(KernelTickMs() - t0) < 3) ;
    } else {
        i2c_setup();
    }
    return err;
}

/* START + address; returns with ADDR set (not yet cleared) */
static int i2c_start(uint8_t addr, uint8_t rw, uint32_t t0, uint32_t timeout_ms)
{
    int r;
    if ((r = i2c_wait_idle(t0, timeout_ms)) != I2C_OK) return r;
    I2C_SR1(BUS) = 0;               /* clear stale error flags */
    i2c_send_start(BUS);
    if ((r = i2c_wait(I2C_SR1_SB, t0, timeout_ms)) != I2C_OK) return r;
    i2c_send_7bit_address(BUS, addr, rw);
    return i2c_wait(I2C_SR1_ADDR, t0, timeout_ms);
}

int I2cWrite(uint8_t addr, const uint8_t *buf, int len, uint32_t timeout_ms)
{
    uint32_t t0 = KernelTickMs();
    int r;

    if ((r = i2c_start(addr, I2C_WRITE, t0, timeout_ms)) != I2C_OK) return i2c_fail(r);
    (void)I2C_SR2(BUS);                             /* clear ADDR */
    for (int i = 0; i < len; i++) {
        if ((r = i2c_wait(I2C_SR1_TxE, t0, timeout_ms)) != I2C_OK) return i2c_fail(r);
        i2c_send_data(BUS, buf[i]);
    }
    if ((r = i2c_wait(I2C_SR1_BTF, t0, timeout_ms)) != I2C_OK) return i2c_fail(r);
    i2c_send_stop(BUS);
    return I2C_OK;
}

int I2cRead(uint8_t addr, uint8_t *buf, int len, uint32_t timeout_ms)
{
    uint32_t t0 = KernelTickMs();
    int r, i = 0;

    if (len <= 0) return I2C_OK;

    if (len == 2) {
        I2C_CR1(BUS) |= I2C_CR1_POS | I2C_CR1_ACK;
    } else {
        I2C_CR1(BUS) &= ~I2C_CR1_POS;
        i2c_enable_ack(BUS);
    }

    if ((r = i2c_start(addr, I2C_READ, t0, timeout_ms)) != I2C_OK) return i2c_fail(r);

    if (len == 1) {
        i2c_disable_ack(BUS);
        (void)I2C_SR2(BUS);                         /* clear ADDR */
        i2c_send_stop(BUS);
        if ((r = i2c_wait(I2C_SR1_RxNE, t0, timeout_ms)) != I2C_OK) return i2c_fail(r);
        buf[0] = i2c_get_data(BUS);
    } else if (len == 2) {
        (void)I2C_SR2(BUS);                         /* clear ADDR */
        i2c_disable_ack(BUS);
        if ((r = i2c_wait(I2C_SR1_BTF, t0, timeout_ms)) != I2C_OK) return i2c_fail(r);
        i2c_send_stop(BUS);
        buf[0] = i2c_get_data(BUS);
        buf[1] = i2c_get_data(BUS);
        I2C_CR1(BUS) &= ~I2C_CR1_POS;
    } else {
        (void)I2C_SR2(BUS);                         /* clear ADDR */
        while (len - i > 3) {
            if ((r = i2c_wait(I2C_SR1_RxNE, t0, timeout_ms)) != I2C_OK) return i2c_fail(r);
            buf[i++] = i2c_get_data(BUS);
        }
        /* three left: wait for DR = N-2, shift = N-1 (SCL stretched by us) */
        if ((r = i2c_wait(I2C_SR1_BTF, t0, timeout_ms)) != I2C_OK) return i2c_fail(r);
        i2c_disable_ack(BUS);
        buf[i++] = i2c_get_data(BUS);               /* N-2; N starts with NACK */
        i2c_send_stop(BUS);
        buf[i++] = i2c_get_data(BUS);               /* N-1 */
        if ((r = i2c_wait(I2C_SR1_RxNE, t0, timeout_ms)) != I2C_OK) return i2c_fail(r);
        buf[i++] = i2c_get_data(BUS);               /* N */
    }
    return I2C_OK;
}

/* Bus recovery: a slave left mid-byte holds SDA low.  Drive SCL as GPIO
 * for up to 9 clocks until SDA is released, issue a STOP, re-init. */
int I2cRecover(void)
{
    i2c_peripheral_disable(BUS);
    gpio_mode_setup(I2C_Port, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, I2C_SCL_Pin);
    gpio_mode_setup(I2C_Port, GPIO_MODE_INPUT,  GPIO_PUPD_PULLUP, I2C_SDA_Pin);
    gpio_set_output_options(I2C_Port, GPIO_OTYPE_OD, GPIO_OSPEED_2MHZ, I2C_SCL_Pin);

    for (int n = 0; n < 9 && !gpio_get(I2C_Port, I2C_SDA_Pin); n++) {
        gpio_clear(I2C_Port, I2C_SCL_Pin);
        for (volatile int d = 0; d < 500; d++) ;    /* ~5 us at 96 MHz */
        gpio_set(I2C_Port, I2C_SCL_Pin);
        for (volatile int d = 0; d < 500; d++) ;
    }
    /* STOP: SDA low -> high while SCL high */
    gpio_mode_setup(I2C_Port, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, I2C_SDA_Pin);
    gpio_set_output_options(I2C_Port, GPIO_OTYPE_OD, GPIO_OSPEED_2MHZ, I2C_SDA_Pin);
    gpio_clear(I2C_Port, I2C_SDA_Pin);
    for (volatile int d = 0; d < 500; d++) ;
    gpio_set(I2C_Port, I2C_SDA_Pin);
    for (volatile int d = 0; d < 500; d++) ;
    int released = gpio_get(I2C_Port, I2C_SDA_Pin) ? 1 : 0;

    I2cInit(i2c_khz_cur);
    return released ? 0 : -1;
}
