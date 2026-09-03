#ifndef _SIM_H_INCLUDED_
#define _SIM_H_INCLUDED_

/* sim.h -- on-board frame simulator: synthetic F103-format frames
 * ([usfmMeasureReply][UPS][DNS]) so the PC receiver and the USB path can
 * be exercised without the MSP430.  Frames alternate burst form A/B and
 * carry a small, known dToF so a downstream calculator has something to
 * find. */

#include <stdint.h>

/* Builds a frame into out (max bytes), returns its length. */
int SimBuildFrame(uint8_t *out, int max);

#endif /* _SIM_H_INCLUDED_ */
