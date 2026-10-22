#ifndef __dsp_declick_h__
#define __dsp_declick_h__

#include "typedefsTGDS.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ARM7 Direct Sound de-clicking / DAC smoothing.
 *
 * The SNES DSP produces step discontinuities (key-on/key-off, APU reset, and
 * rapid map/sample-format transitions).  Those steps reach the NDS DAC almost
 * unfiltered and are heard as pops/crackle.  Everything below is integer-only,
 * allocation-free and strictly bounded so it cannot introduce latency drift.
 */

/* Reset the smoothing state (call on APU reset / sound reinit). */
extern void DspDeclickInit(void);

/* Request a bounded ramp to silence over the next DECLICK_RAMP panels
 * (used for APU reset / map transitions so the change is not a hard step). */
extern void DspDeclickRequestFade(void);

/*
 * Post-process one freshly mixed panel.
 *   dst      = &playBuffer[soundCursor]  (Left half of the panel)
 *   samples  = panel length (MIXBUFSIZE)
 * The Right half lives at dst + (MIXBUFSIZE << 1) s16, matching the layout
 * produced by DspMixSamplesStereo / clipAndMix and consumed by the hardware.
 */
extern void DspDeclickPanel(s16 *dst, int samples);

/* Number of panels still being ramped to silence (0 = normal playback). */
extern int dspDeclickFade;

#ifdef __cplusplus
}
#endif

#endif
