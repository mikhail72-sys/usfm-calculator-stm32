/* stream.c -- packs frames into USB records, see stream.h */
#include <string.h>
#include "stream.h"
#include "usb_vcp.h"
#include "usfm_link.h"
#include "kernel.h"

_Static_assert(sizeof(usfmStreamHdr) == 24, "usfmStreamHdr must be 24 bytes");
_Static_assert(sizeof(usfmCalcCfg) == 16, "usfmCalcCfg must be 16 bytes");
_Static_assert(sizeof(usfmCalcState) == 56, "usfmCalcState must be 56 bytes");
_Static_assert(sizeof(usfmMeasureReply) == 16, "usfmMeasureReply v6 must be 16 bytes");

static uint32_t seq, dropped, records;
static bool     drop_pending;

void StreamInit(void)
{
    seq = 0; dropped = 0; records = 0; drop_pending = false;
}

static uint16_t err_i2c_total(void)
{
    const usfm_link_stats_t *s = UsfmLinkStats();
    return (uint16_t)(s->i2c_nack + s->i2c_timeout + s->i2c_bus + s->crc_err + s->format_err);
}

bool StreamPush(const uint8_t *payload, int len, uint16_t flags, const usfmCalcResult *res)
{
    usfmStreamHdr h;
    int rlen = res ? (int)sizeof(*res) : 0;
    int total = (int)sizeof(h) + len + rlen + 2;

    if (usb_vcp_tx_free() < total) {
        dropped++;
        drop_pending = true;
        return false;
    }

    h.magic      = USFM_STREAM_MAGIC;
    h.hdr_ver    = USFM_STREAM_HDR_VER;
    h.hdr_len    = sizeof(h);
    h.seq        = seq++;
    h.tick_ms    = KernelTickMs();
    h.frame_len  = (uint16_t)(len + rlen);
    h.result_len = (uint16_t)rlen;
    h.flags      = flags | (drop_pending ? USFM_STREAM_F_DROPPED : 0) | (res ? USFM_STREAM_F_CALC : 0);
    h.err_i2c    = err_i2c_total();
    drop_pending = false;

    uint16_t crc = UsfmCrc16((const uint8_t *)&h, sizeof(h));
    crc = UsfmCrc16Update(crc, payload, len);
    if (res) crc = UsfmCrc16Update(crc, (const uint8_t *)res, rlen);
    uint8_t crcb[2] = { (uint8_t)crc, (uint8_t)(crc >> 8) };

    usb_vcp_write((const uint8_t *)&h, sizeof(h));
    usb_vcp_write(payload, len);
    if (res) usb_vcp_write((const uint8_t *)res, rlen);
    usb_vcp_write(crcb, 2);
    records++;
    return true;
}

void StreamReply(const uint8_t *frame, int len)
{
    uint8_t pre[6];
    uint32_t m = USFM_REPLY_MAGIC;
    memcpy(pre, &m, 4);
    pre[4] = (uint8_t)len;
    pre[5] = (uint8_t)(len >> 8);
    if (usb_vcp_tx_free() < 6 + len) { dropped++; return; }
    usb_vcp_write(pre, 6);
    usb_vcp_write(frame, len);
}

uint32_t StreamSeq(void)     { return seq; }
uint32_t StreamDropped(void) { return dropped; }
uint32_t StreamRecords(void) { return records; }
