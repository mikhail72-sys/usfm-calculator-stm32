/* usfmcalc.c -- application entry, shared by every MCU target.
 *
 * Stage 1 (transit): frames from the MSP430 over I2C -> USB record stream
 * to the PC, plus an on-board simulator so the PC side can be exercised
 * without the meter.  No mathematics yet.
 *
 * Main loop, all polled, no interrupts besides SysTick:
 *   usb poll -> commands (cmd.c) -> frame source -> stream push -> LED
 *
 * Frame source:
 *   I2C  : when DRDY is high (or every cfg.poll_ms), F104 GET_FRAME;
 *          a frame whose capture_seq equals the previous one is not
 *          re-sent; after a failed exchange the bus is left alone for
 *          I2C_ERR_BACKOFF_MS.
 *   SIM  : sim.c frame every 1000/sim_rate ms.
 *
 * LED (PC13):
 *   slow blink 1 Hz        USB not configured
 *   short blip every 1 s   USB up, stream off
 *   solid on               stream on, flickers off on every record
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "config.h"
#include "kernel.h"
#include "usb_vcp.h"
#include "usfm_link.h"
#include "stream.h"
#include "sim.h"
#include "cmd.h"

#define FLICKER_MS  30

static uint8_t  simbuf[sizeof(usfmMeasureReply) + 2 * 2 * SIM_SAMPLES];
static uint32_t frames_in;
static uint16_t last_seq = 0xFFFF;

static void led_task(uint32_t now, uint32_t last_traffic)
{
    uint32_t phase = now % 1000;

    if (!usb_vcp_configured())      LedSet(phase < 500);
    else if (!calc_cfg.stream_on)   LedSet(phase < 50);
    else                            LedSet((uint32_t)(now - last_traffic) > FLICKER_MS);
}

/* One F104 exchange; returns true if a new frame went out. */
static bool take_i2c_frame(void)
{
    const uint8_t *pl;
    int len = UsfmLinkRequest(USFM_F_GET_FRAME, NULL, 0, &pl);
    if (len < (int)sizeof(usfmMeasureReply)) return false;

    const usfmMeasureReply *r = (const usfmMeasureReply *)pl;
    if (len != (int)sizeof(*r) + 4 * r->sample_size) return false;   /* malformed */
    if (r->capture_seq == last_seq) return false;                     /* seen already */
    last_seq = r->capture_seq;
    frames_in++;
    StreamPush(pl, len, 0, NULL);
    return true;
}

int main(void)
{
    uint32_t last_traffic = 0, last_poll = 0, last_sim = 0, backoff_until = 0;

    KernelInit();
    CmdInit();
    I2cInit(calc_cfg.i2c_khz);
    StreamInit();
    usb_vcp_init();

    for (;;) {
        usb_vcp_poll();
        CmdPoll();

        uint32_t now = KernelTickMs();

        if (calc_cfg.stream_on && usb_vcp_configured()) {
            if (calc_cfg.source == CALC_SRC_SIM) {
                uint32_t period = 1000 / calc_cfg.sim_rate;
                if (period == 0) period = 1;
                if ((uint32_t)(now - last_sim) >= period) {
                    last_sim = now;
                    int len = SimBuildFrame(simbuf, sizeof(simbuf));
                    frames_in++;
                    if (StreamPush(simbuf, len, USFM_STREAM_F_SYNTH, NULL))
                        last_traffic = now;
                }
            } else if ((int32_t)(now - backoff_until) >= 0) {
                bool due = DrdyActive() ||
                           (calc_cfg.poll_ms && (uint32_t)(now - last_poll) >= calc_cfg.poll_ms);
                if (due) {
                    last_poll = now;
                    const usfm_link_stats_t *s = UsfmLinkStats();
                    uint32_t fails = s->i2c_nack + s->i2c_timeout + s->i2c_bus;
                    if (take_i2c_frame()) last_traffic = now;
                    if (UsfmLinkStats()->i2c_nack + UsfmLinkStats()->i2c_timeout +
                        UsfmLinkStats()->i2c_bus != fails)
                        backoff_until = now + I2C_ERR_BACKOFF_MS;
                }
            }
        }

        calc_state.frames_in = frames_in;
        calc_state.last_seq  = last_seq;
        led_task(now, last_traffic);
    }
}
