#ifndef _CMD_H_INCLUDED_
#define _CMD_H_INCLUDED_

/* cmd.h -- command channel PC -> STM32 over the USB VCP (see stream.h):
 *   [len16][addr][func][payload][crc16]
 * addr USFM_CALC_ADDR: served here (F17 passport, F100 register blocks
 * 4000/4100/4200); addr USFM_DEFAULT_ADDR: bridged verbatim to the MSP430
 * over I2C, its reply comes back wrapped by StreamReply(). */

#include <stdint.h>
#include "stream.h"

void CmdInit(void);
void CmdPoll(void);                 /* drains the USB RX ring, dispatches complete frames */

/* the calculator's live configuration, owned here, read by main */
extern usfmCalcCfg   calc_cfg;
extern usfmCalcState calc_state;    /* filled by main before each reply (CmdRefreshState) */
extern usfmCalcResult calc_last;

/* main supplies live counters before a state read */
void CmdRefreshState(void);

#endif /* _CMD_H_INCLUDED_ */
