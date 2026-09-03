/* sim.c -- synthetic F103-format frames, see sim.h */
#include <string.h>
#include <math.h>
#include "sim.h"
#include "config.h"
#include "protocol.h"

#define FS_HZ           4000000.0f      /* SDHS grid, 4 Msps */
#define F1_HZ           1000000.0f      /* carrier */
#define BURST_START     60              /* first sample of the echo */
#define BURST_PERIODS   10              /* echo length, carrier periods */
#define AMPL            1500.0f
#define DTOF_SAMPLES    0.25f           /* DNS lags UPS by this (62.5 ns) */
#define NOISE_LSB       6

static uint16_t capture_seq;
static uint32_t lcg = 0x12345678;

static int noise(void)
{
    lcg = lcg * 1664525u + 1013904223u;
    return (int)(lcg >> 28) - 8;       /* -8..7 */
}

/* windowed sine burst arriving at t0 (in samples, fractional) */
static void fill(int16_t *dst, int n, float t0)
{
    const float burst_len = BURST_PERIODS * FS_HZ / F1_HZ;     /* samples */
    for (int k = 0; k < n; k++) {
        float t = (float)k - t0;
        float v = 0.0f;
        if (t >= 0.0f && t < burst_len) {
            float w = sinf(3.14159265f * t / burst_len);        /* half-sine envelope */
            v = AMPL * w * sinf(2.0f * 3.14159265f * F1_HZ / FS_HZ * t);
        }
        int s = (int)v + noise() * NOISE_LSB / 8;
        dst[k] = (int16_t)s;
    }
}

int SimBuildFrame(uint8_t *out, int max)
{
    const int n = SIM_SAMPLES;
    const int len = (int)sizeof(usfmMeasureReply) + 2 * 2 * n;
    if (max < len) return 0;

    usfmMeasureReply r;
    memset(&r, 0, sizeof(r));
    r.status       = USFM_MEAS_ST_SYNTH_DATA;
    r.capture_seq  = ++capture_seq;
    r.sample_size  = (uint16_t)n;
    r.adc_start_us = 100;
    r.capture_us   = (uint16_t)(n * 1000000UL / 4000000UL);
    r.hv_mv        = 30000;
    /* alternate forms A/B (dual / barker), UPS first, hord 0 */
    r.frame_flags  = (uint16_t)((capture_seq & 1) ? USFM_BURST_BARKER : USFM_BURST_DUAL);

    memcpy(out, &r, sizeof(r));
    int16_t *ups = (int16_t *)(out + sizeof(r));
    int16_t *dns = ups + n;
    fill(ups, n, (float)BURST_START);
    fill(dns, n, (float)BURST_START + DTOF_SAMPLES);
    return len;
}
