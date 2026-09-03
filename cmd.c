/* cmd.c -- command channel PC -> STM32, see cmd.h */
#include <string.h>
#include "cmd.h"
#include "usb_vcp.h"
#include "usfm_link.h"
#include "kernel.h"
#include "config.h"

#define CMD_MAX         (2 + 4 + 255 + 4)   /* len16 + addr/func + F100 max + crc slack */

usfmCalcCfg    calc_cfg;
usfmCalcState  calc_state;
usfmCalcResult calc_last;

static const usfmInfo calc_info = {
    .name       = "USFMCALC",
    .fw_ver     = CALC_FW_VER,
    .proto_ver  = USFM_PROTO_VER,
    .capability = 0,
};

static uint8_t  rx[CMD_MAX];
static int      rxn;
static uint8_t  reply[4 + 260];

void CmdInit(void)
{
    memset(&calc_cfg, 0, sizeof(calc_cfg));
    calc_cfg.stream_on = 0;
    calc_cfg.source    = CALC_SRC_I2C;
    calc_cfg.sim_rate  = SIM_RATE_DEFAULT;
    calc_cfg.i2c_khz   = I2C_KHZ_DEFAULT;
    calc_cfg.poll_ms   = 0;
    memset(&calc_last, 0, sizeof(calc_last));
    rxn = 0;
}

/* ---- reply helpers: frame = [addr][func][payload][crc] ---- */
static void send_frame(uint8_t addr, uint8_t func, const uint8_t *payload, int plen)
{
    reply[0] = addr;
    reply[1] = func;
    if (plen) memcpy(&reply[2], payload, (size_t)plen);
    uint16_t crc = UsfmCrc16(reply, 2 + plen);
    reply[2 + plen] = (uint8_t)crc;
    reply[3 + plen] = (uint8_t)(crc >> 8);
    StreamReply(reply, 4 + plen);
}

static void send_exception(uint8_t addr, uint8_t func, uint8_t code)
{
    send_frame(addr, func | 0x80, &code, 1);
}

/* ---- register map of the calculator ---- */
typedef struct { int base; void *p; int size; int writable; } block_t;

static usfmLinkRaw link_raw;

static void refresh_linkraw(void)
{
    int n;
    const uint8_t *f = UsfmLinkLastFrame(&n);
    link_raw.len = (uint16_t)n;
    link_raw.last_i2c_err = (uint16_t)UsfmLinkStats()->last_i2c_err;
    memset(link_raw.data, 0, sizeof(link_raw.data));
    memcpy(link_raw.data, f, n > (int)sizeof(link_raw.data) ? sizeof(link_raw.data) : (size_t)n);
}

static const block_t *find_block(int reg)
{
    static block_t blocks[4];
    blocks[0] = (block_t){ USFM_REG_CALC_CFG,     &calc_cfg,   sizeof(calc_cfg),   1 };
    blocks[1] = (block_t){ USFM_REG_CALC_STATE,   &calc_state, sizeof(calc_state), 0 };
    blocks[2] = (block_t){ USFM_REG_CALC_LAST,    &calc_last,  sizeof(calc_last),  0 };
    blocks[3] = (block_t){ USFM_REG_CALC_LINKRAW, &link_raw,   sizeof(link_raw),   0 };
    for (int i = 0; i < 4; i++)
        if (reg >= blocks[i].base && reg < blocks[i].base + blocks[i].size)
            return &blocks[i];
    return NULL;
}

static void apply_cfg(void)
{
    if (calc_cfg.i2c_khz != 100 && calc_cfg.i2c_khz != 400) calc_cfg.i2c_khz = I2C_KHZ_DEFAULT;
    if (calc_cfg.sim_rate < 1) calc_cfg.sim_rate = 1;
    if (calc_cfg.sim_rate > 1000) calc_cfg.sim_rate = 1000;
    if (calc_cfg.source > CALC_SRC_SIM) calc_cfg.source = CALC_SRC_I2C;
    I2cInit(calc_cfg.i2c_khz);
}

static void serve_calc(uint8_t addr, uint8_t func, const uint8_t *pl, int plen)
{
    if (func == USFM_F_SLAVE_ID) {
        uint8_t buf[1 + sizeof(usfmInfo)];
        buf[0] = sizeof(usfmInfo);
        memcpy(&buf[1], &calc_info, sizeof(usfmInfo));
        send_frame(addr, func, buf, sizeof(buf));
        return;
    }
    if (func == USFM_F_SERVICE) {
        if (plen < USFM_CMD100_HDR_SIZE) { send_exception(addr, func, USFM_ERR_VALUE); return; }
        int16_t reg = (int16_t)(pl[0] | (pl[1] << 8));
        int len = pl[2];
        if (reg >= 0) {
            const block_t *b = find_block(reg);
            if (!b || reg + len > b->base + b->size) { send_exception(addr, func, USFM_ERR_ADDRESS); return; }
            if (b->p == &calc_state) CmdRefreshState();
            if (b->p == &link_raw)   refresh_linkraw();
            send_frame(addr, func, (const uint8_t *)b->p + (reg - b->base), len);
        } else {
            reg = (int16_t)(-reg);
            const block_t *b = find_block(reg);
            if (!b || reg + len > b->base + b->size) { send_exception(addr, func, USFM_ERR_ADDRESS); return; }
            if (!b->writable) { send_exception(addr, func, USFM_ERR_ADDRESS); return; }
            if (plen < USFM_CMD100_HDR_SIZE + len) { send_exception(addr, func, USFM_ERR_VALUE); return; }
            memcpy((uint8_t *)b->p + (reg - b->base), &pl[3], (size_t)len);
            apply_cfg();
            send_exception(addr, func, USFM_ERR_BUSY);     /* "accepted", uslm heritage */
        }
        return;
    }
    if (func == USFM_F_CALC_RAWLINK) {
        static uint8_t raw[4 + 2 + CALC_RAWLINK_MAX + 2];
        if (plen < 4) { send_exception(addr, func, USFM_ERR_VALUE); return; }
        int rd = pl[0] | (pl[1] << 8);
        int delay_ms = pl[2];
        if (rd > CALC_RAWLINK_MAX) { send_exception(addr, func, USFM_ERR_VALUE); return; }
        int r = I2cWrite(USFM_I2C_ADDR, &pl[3], plen - 3, I2C_TMO_WRITE_MS);
        uint32_t t0 = KernelTickMs();
        while ((uint32_t)(KernelTickMs() - t0) < (uint32_t)delay_ms) ;
        if (r == I2C_OK) r = I2cRead(USFM_I2C_ADDR, &raw[2], 2, I2C_TMO_LEN_MS);
        if (r == I2C_OK && rd > 0) r = I2cRead(USFM_I2C_ADDR, &raw[4], rd, I2C_TMO_BODY_MS);
        if (r != I2C_OK) { send_exception(addr, func, USFM_ERR_FAILURE); return; }
        raw[0] = addr; raw[1] = func;
        uint16_t crc = UsfmCrc16(raw, 4 + rd);
        raw[4 + rd] = (uint8_t)crc; raw[5 + rd] = (uint8_t)(crc >> 8);
        StreamReply(raw, 6 + rd);
        return;
    }
    send_exception(addr, func, USFM_ERR_FUNCTION);
}

static void bridge_to_msp(uint8_t func, const uint8_t *pl, int plen)
{
    const uint8_t *rp;
    int r = UsfmLinkRequest(func, pl, plen, &rp);
    /* CRC failures are passed through raw as well: the PC sees the bad CRC
     * itself and can tell where a long read went wrong (bring-up aid). */
    if (r >= 0 || r == LINK_ERR_EXCEPTION || r == LINK_ERR_CRC) {
        int flen;
        const uint8_t *f = UsfmLinkLastFrame(&flen);
        StreamReply(f, flen);                       /* the slave's frame verbatim */
        return;
    }
    /* link failure: synthesize an exception so the PC sees something */
    send_exception(USFM_DEFAULT_ADDR, func, USFM_ERR_FAILURE);
}

static void dispatch(const uint8_t *f, int len)
{
    /* f = [addr][func][payload][crc], len >= 4, CRC already verified */
    uint8_t addr = f[0], func = f[1];
    const uint8_t *pl = &f[2];
    int plen = len - 4;

    if (addr == USFM_CALC_ADDR || addr == USFM_ADDR_ANY)
        serve_calc(USFM_CALC_ADDR, func, pl, plen);
    else if (addr == USFM_DEFAULT_ADDR)
        bridge_to_msp(func, pl, plen);
    /* other addresses: silently ignored, as on a shared bus */
}

void CmdPoll(void)
{
    uint8_t tmp[64];
    int n;

    while ((n = usb_vcp_read(tmp, sizeof(tmp))) > 0) {
        for (int i = 0; i < n; i++) {
            if (rxn < CMD_MAX) rx[rxn++] = tmp[i]; else rxn = 0;

            for (;;) {
                if (rxn < 2) break;
                int flen = rx[0] | (rx[1] << 8);
                if (flen < 4 || flen > CMD_MAX - 2) {       /* junk: slide by one */
                    memmove(rx, rx + 1, (size_t)(rxn - 1)); rxn--;
                    continue;
                }
                if (rxn < 2 + flen) break;                  /* wait for the rest */
                uint16_t want = (uint16_t)(rx[2 + flen - 2] | (rx[2 + flen - 1] << 8));
                if (UsfmCrc16(rx + 2, flen - 2) == want) {
                    dispatch(rx + 2, flen);
                    memmove(rx, rx + 2 + flen, (size_t)(rxn - 2 - flen)); rxn -= 2 + flen;
                } else {
                    memmove(rx, rx + 1, (size_t)(rxn - 1)); rxn--;   /* resync */
                }
            }
        }
    }
}

void CmdRefreshState(void)
{
    const usfm_link_stats_t *s = UsfmLinkStats();
    calc_state.uptime_s     = KernelTickMs() / 1000;
    calc_state.records_out  = StreamRecords();
    calc_state.dropped      = StreamDropped();
    calc_state.i2c_requests = s->requests;
    calc_state.i2c_ok       = s->ok;
    calc_state.i2c_nack     = s->i2c_nack;
    calc_state.i2c_timeout  = s->i2c_timeout;
    calc_state.i2c_bus      = s->i2c_bus;
    calc_state.crc_err      = s->crc_err;
    calc_state.format_err   = s->format_err;
    calc_state.exceptions   = s->exceptions;
    calc_state.drdy         = (uint16_t)DrdyActive();
    calc_state.usb_configured = usb_vcp_configured();
    calc_state.usb_dtr      = usb_vcp_dtr();
    /* frames_in and last_seq are maintained by main */
}
