/* usfm_link.c -- USFM protocol frames over the I2C link to the MSP430.
 * See usfm_link.h for the wire form. */
#include <string.h>
#include "usfm_link.h"
#include "kernel.h"
#include "config.h"

static uint8_t  txbuf[4 + USFM_I2C_WRITE_MAX];
static uint8_t  rxbuf[LINK_BUF_SIZE];
static int      rxlen;
static uint8_t  exc_code;
static usfm_link_stats_t st;

uint16_t UsfmCrc16Update(uint16_t crc, const uint8_t *p, int n)
{
    while (n-- > 0) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++)
            crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
    }
    return crc;
}

uint16_t UsfmCrc16(const uint8_t *p, int n)
{
    return UsfmCrc16Update(0xFFFF, p, n);
}

static int i2c_failed(int r)
{
    st.last_i2c_err = (uint32_t)r;
    switch (r) {
    case I2C_ERR_NACK:    st.i2c_nack++;    break;
    case I2C_ERR_TIMEOUT: st.i2c_timeout++; break;
    default:              st.i2c_bus++;     break;
    }
    return LINK_ERR_I2C;
}

int UsfmLinkRequest(uint8_t func, const uint8_t *payload, int plen, const uint8_t **reply)
{
    int r;
    uint8_t lenb[2];

    rxlen = 0;
    if (reply) *reply = NULL;
    if (plen < 0 || plen > USFM_I2C_WRITE_MAX) return LINK_ERR_PARAM;
    st.requests++;

    /* request frame */
    txbuf[0] = USFM_DEFAULT_ADDR;
    txbuf[1] = func;
    if (plen) memcpy(&txbuf[2], payload, (size_t)plen);
    uint16_t crc = UsfmCrc16(txbuf, 2 + plen);
    txbuf[2 + plen] = (uint8_t)crc;
    txbuf[3 + plen] = (uint8_t)(crc >> 8);

    if ((r = I2cWrite(USFM_I2C_ADDR, txbuf, 4 + plen, I2C_TMO_WRITE_MS)) != I2C_OK)
        return i2c_failed(r);

    /* Measured on fw 1.60 (2026-09-03): a read that follows the request's
     * STOP within microseconds returns the PREVIOUS reply (the slave parses
     * the request in its main loop, no SCL stretching covers that gap);
     * >= 1 ms is always fresh.  Give it I2C_REPLY_DELAY_MS. */
    uint32_t t0 = KernelTickMs();
    while ((uint32_t)(KernelTickMs() - t0) < I2C_REPLY_DELAY_MS) ;

    /* length prefix */
    if ((r = I2cRead(USFM_I2C_ADDR, lenb, 2, I2C_TMO_LEN_MS)) != I2C_OK)
        return i2c_failed(r);
    int len = lenb[0] | (lenb[1] << 8);
    if (len == 0) { st.format_err++; return LINK_ERR_EMPTY; }
    if (len > (int)sizeof(rxbuf)) {
        /* drain it anyway so the slave's pointer/DRDY complete, then drop */
        int left = len;
        while (left > 0) {
            int chunk = left > (int)sizeof(rxbuf) ? (int)sizeof(rxbuf) : left;
            if (I2cRead(USFM_I2C_ADDR, rxbuf, chunk, I2C_TMO_BODY_MS) != I2C_OK) break;
            left -= chunk;
        }
        st.format_err++;
        return LINK_ERR_TOOLONG;
    }

    /* Body.  Observed on fw 1.60 (2026-09-03): every new START restarts the
     * slave's reply from the length prefix, so the body read returns
     * [len_lo][len_hi][frame].  protocol.h describes a continuous pointer
     * (body only).  Read len+2 and accept either: if the first two bytes
     * repeat the prefix, skip them. */
    if ((r = I2cRead(USFM_I2C_ADDR, rxbuf, len + 2, I2C_TMO_BODY_MS)) != I2C_OK)
        return i2c_failed(r);
    if (rxbuf[0] == lenb[0] && rxbuf[1] == lenb[1] && rxbuf[2] == USFM_DEFAULT_ADDR) {
        memmove(rxbuf, rxbuf + 2, (size_t)len);
        st.prefix_repeats++;
    }
    rxlen = len;

    if (len < 4) { st.format_err++; return LINK_ERR_FORMAT; }
    uint16_t got = (uint16_t)(rxbuf[len - 2] | (rxbuf[len - 1] << 8));
    if (UsfmCrc16(rxbuf, len - 2) != got) { st.crc_err++; return LINK_ERR_CRC; }
    if (rxbuf[0] != USFM_DEFAULT_ADDR) { st.format_err++; return LINK_ERR_FORMAT; }
    if (rxbuf[1] == (uint8_t)(func | 0x80)) {
        exc_code = (len >= 5) ? rxbuf[2] : 0;
        st.exceptions++;
        return LINK_ERR_EXCEPTION;
    }
    if (rxbuf[1] != func) { st.format_err++; return LINK_ERR_FORMAT; }

    st.ok++;
    if (reply) *reply = &rxbuf[2];
    return len - 4;
}

const uint8_t *UsfmLinkLastFrame(int *len)
{
    if (len) *len = rxlen;
    return rxbuf;
}

uint8_t UsfmLinkExcCode(void)               { return exc_code; }
const usfm_link_stats_t *UsfmLinkStats(void) { return &st; }
