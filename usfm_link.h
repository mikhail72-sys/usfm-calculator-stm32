#ifndef _USFM_LINK_H_INCLUDED_
#define _USFM_LINK_H_INCLUDED_

/* usfm_link.h -- USFM protocol frames over the I2C link to the MSP430.
 *
 * Wire form (protocol.h v6, "I2C-отвод к вычислителю"):
 *   master write : [addr=1][func][payload][crc_lo][crc_hi]   (<= 64 B payload)
 *   master read  : [len_lo][len_hi] then the reply frame [addr][func][...][crc]
 *                  (len = frame bytes without the prefix; 0 = nothing to read)
 * CRC16 Modbus (0xA001, init 0xFFFF) over addr..payload, as on the UART.
 */

#include <stdint.h>
#include "protocol.h"

/* Result codes of UsfmLinkRequest (negative); >= 0 is the payload length */
#define LINK_ERR_I2C        (-1)    /* bus failure, see UsfmLinkStats */
#define LINK_ERR_EMPTY      (-2)    /* slave had nothing to say (len = 0) */
#define LINK_ERR_CRC        (-3)    /* reply CRC mismatch */
#define LINK_ERR_FORMAT     (-4)    /* reply too short / wrong addr / wrong func */
#define LINK_ERR_TOOLONG    (-5)    /* reply exceeds the link buffer */
#define LINK_ERR_EXCEPTION  (-6)    /* slave answered func|0x80; code in UsfmLinkExcCode() */
#define LINK_ERR_PARAM      (-7)    /* payload > USFM_I2C_WRITE_MAX */

/* Biggest reply we accept: addr+func + reply header + 2 x 512 samples + crc */
#define LINK_BUF_SIZE       (2 + sizeof(usfmMeasureReply) + 2 * 2 * 512 + 2)

typedef struct {
    uint32_t    requests;       /* exchanges attempted */
    uint32_t    ok;             /* clean replies */
    uint32_t    i2c_nack;       /* address/data NACK (slave absent?) */
    uint32_t    i2c_timeout;    /* SCL stretched too long / bus dead */
    uint32_t    i2c_bus;        /* BERR/ARLO */
    uint32_t    crc_err;
    uint32_t    format_err;     /* short/odd replies, len = 0 counted here too */
    uint32_t    exceptions;     /* protocol-level errors from the slave */
    uint32_t    last_i2c_err;   /* I2C_ERR_* of the last failure */
    uint32_t    prefix_repeats; /* body reads that started with the length prefix again */
} usfm_link_stats_t;

uint16_t UsfmCrc16Update(uint16_t crc, const uint8_t *p, int n);
uint16_t UsfmCrc16(const uint8_t *p, int n);

/* Send [1][func][payload] and fetch the reply.  On success returns the
 * payload length of the reply and sets *payload to it (points into the
 * link buffer, valid until the next call); the whole reply frame
 * [addr][func][payload][crc] is available through UsfmLinkLastFrame(). */
int  UsfmLinkRequest(uint8_t func, const uint8_t *payload, int plen, const uint8_t **reply);
const uint8_t *UsfmLinkLastFrame(int *len);
uint8_t UsfmLinkExcCode(void);
const usfm_link_stats_t *UsfmLinkStats(void);

#endif /* _USFM_LINK_H_INCLUDED_ */
