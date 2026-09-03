#ifndef _STREAM_H_INCLUDED_
#define _STREAM_H_INCLUDED_

/* stream.h -- USB record stream from the calculator to the PC and the
 * calculator's own register blocks (F100 on address USFM_CALC_ADDR).
 *
 * Draft: inbox/AGREED_usb-stream.md.  Everything little-endian, no
 * padding holes.  Candidates for protocol.h at the next proto_ver++.
 *
 * Record (STM32 -> PC):
 *   [usfmStreamHdr][usfmMeasureReply][UPS int16*n][DNS int16*n][result][crc16]
 *   crc16 Modbus over everything before it.
 * Command reply (STM32 -> PC), same byte stream:
 *   [magic "USFR"][len16][frame [addr][func][payload][crc16]]
 * Command (PC -> STM32):
 *   [len16][frame [addr][func][payload][crc16]]
 *   addr 2 = the calculator, addr 1 = forwarded to the MSP430 over I2C.
 */

#include <stdint.h>
#include <stdbool.h>
#include "protocol.h"

#define USFM_STREAM_MAGIC       (0x4D465355UL)  /* "USFM" as bytes */
#define USFM_REPLY_MAGIC        (0x52465355UL)  /* "USFR" as bytes */
#define USFM_STREAM_HDR_VER     (1)
#define USFM_CALC_ADDR          (2)             /* protocol address of the calculator */

#define USFM_STREAM_F_SYNTH     (0x0001)    /* frame made by the on-board simulator */
#define USFM_STREAM_F_DROPPED   (0x0002)    /* records were dropped before this one (USB full) */
#define USFM_STREAM_F_I2C_ERR   (0x0004)    /* the I2C exchange for this frame needed retries */
#define USFM_STREAM_F_CALC      (0x0008)    /* result block is filled by the calculator */

typedef struct
{
    uint32_t    magic;          /* USFM_STREAM_MAGIC */
    uint16_t    hdr_ver;        /* USFM_STREAM_HDR_VER */
    uint16_t    hdr_len;        /* sizeof(usfmStreamHdr) = 24 */
    uint32_t    seq;            /* record counter, 0 at stream start; gap = loss */
    uint32_t    tick_ms;        /* STM32 SysTick when the frame was taken (rate only) */
    uint16_t    frame_len;      /* bytes after the header up to crc: reply + 4n + result_len */
    uint16_t    result_len;     /* 0 = no result block */
    uint16_t    flags;          /* USFM_STREAM_F_* */
    uint16_t    err_i2c;        /* cumulative I2C failure count since power-up */
} usfmStreamHdr;

/* ---- calculator register map (F100 on addr 2) ---- */
#define USFM_REG_CALC_CFG       (4000)  /* usfmCalcCfg    r/w */
#define USFM_REG_CALC_STATE     (4100)  /* usfmCalcState  r/o */
#define USFM_REG_CALC_LAST      (4200)  /* usfmCalcResult r/o, last result (zeros in stage 1) */
#define USFM_REG_CALC_LINKRAW   (4300)  /* usfmLinkRaw r/o, last raw I2C reply frame (debug) */

typedef struct
{
    uint16_t    len;            /* bytes the slave announced / we read */
    uint16_t    last_i2c_err;   /* I2C_ERR_* of the last failed exchange */
    uint8_t     data[64];       /* first 64 bytes of the raw reply frame */
} usfmLinkRaw;                  /* 68 bytes */

/* Bring-up aid, calculator only (addr 2): raw I2C probe.
 *   request payload : [rd_lo][rd_hi][delay_ms][request frame bytes to write, verbatim]
 *                     delay_ms = pause between the write STOP and the first read
 *   reply payload   : [len_lo][len_hi] (the slave's prefix) + rd raw bytes of
 *                     the body read, no CRC check, no prefix stripping.
 * rd = 0 skips the body read.  Lets the PC see exactly what the slave
 * streams past what it announces. */
#define USFM_F_CALC_RAWLINK     (105)
#define CALC_RAWLINK_MAX        (1200)

#define CALC_SRC_I2C            (0)
#define CALC_SRC_SIM            (1)

typedef struct
{
    uint16_t    stream_on;      /* 0/1: emit records */
    uint16_t    source;         /* CALC_SRC_* */
    uint16_t    sim_rate;       /* simulator frames per second, 1..1000 */
    uint16_t    i2c_khz;        /* 100 / 400, applied on write */
    uint16_t    poll_ms;        /* 0 = DRDY only; else also poll F104 every poll_ms */
    uint16_t    reserve[3];
} usfmCalcCfg;                  /* 16 bytes */

typedef struct
{
    uint32_t    uptime_s;
    uint32_t    frames_in;      /* frames taken (I2C or simulator) */
    uint32_t    records_out;    /* records handed to USB */
    uint32_t    dropped;        /* records dropped: USB TX full */
    uint32_t    i2c_requests;
    uint32_t    i2c_ok;
    uint32_t    i2c_nack;
    uint32_t    i2c_timeout;
    uint32_t    i2c_bus;
    uint32_t    crc_err;
    uint32_t    format_err;
    uint32_t    exceptions;
    uint16_t    drdy;           /* live DRDY level */
    uint16_t    last_seq;       /* capture_seq of the last frame taken */
    uint16_t    usb_configured;
    uint16_t    usb_dtr;
} usfmCalcState;                /* 56 bytes */

/* record side */
void StreamInit(void);
bool StreamPush(const uint8_t *frame_payload, int len, uint16_t flags,
                const usfmCalcResult *res);     /* false = dropped (TX full) */
void StreamReply(const uint8_t *frame, int len); /* wrap a command reply */
uint32_t StreamSeq(void);
uint32_t StreamDropped(void);
uint32_t StreamRecords(void);

#endif /* _STREAM_H_INCLUDED_ */
