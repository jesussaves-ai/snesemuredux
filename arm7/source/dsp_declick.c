/*
 * ARM7 Direct Sound de-clicking / DAC smoothing.
 *
 * See dsp_declick.h for the interface contract.  All maths is done in 32-bit
 * integers; the worst case is a signed 16-bit sample times 256 (~2^23), so
 * there is no overflow and no floating point involved (ARM7TDMI has no FPU).
 *
 * Design: the smoothing is *discontinuity triggered*, so it never touches
 * normal programme material.
 *   - a boundary cross-fade is applied only when the new panel starts with a
 *     step relative to the previous panel (this removes the hardware re-arm /
 *     FIFO-reset seam click while keeping continuous audio bit-exact);
 *   - an explicit transition (DSP reset / map change / stop) ramps the output
 *     to silence over a few panels instead of stepping.
 */

#include "typedefsTGDS.h"
#include "spcdefs.h"
#include "dsp.h"
#include "dsp_declick.h"

/* Length (in samples) of the boundary cross-fade and of the fade-to-silence
 * ramp.  8 samples @ 32 kHz ~= 0.25 ms - long enough to remove the step energy
 * of a click, short enough to be inaudible as a fade. */
#define DECLICK_RAMP     8

/* Only a step larger than this (in 16-bit sample units) is considered a
 * discontinuity worth smoothing.  Continuous material (per-sample deltas well
 * below this) is passed through bit-exact. */
#define DECLICK_STEP     0x1000

/* Last emitted sample of the previous panel (per channel).  Seeds the boundary
 * cross-fade so consecutive panels stay continuous across the hardware seam. */
static s16 declickLastL = 0;
static s16 declickLastR = 0;

int dspDeclickFade = 0;

void DspDeclickInit(void) {
    declickLastL = 0;
    declickLastR = 0;
    dspDeclickFade = 0;
}

void DspDeclickRequestFade(void) {
    /* 8 panels * 63 samples @ 32 kHz ~= 16 ms of ramp. */
    dspDeclickFade = DECLICK_RAMP;
}

void DspDeclickPanel(s16 *dst, int samples) {
    s16 *L = dst;
    s16 *R = dst + (MIXBUFSIZE << 1);
    int i;
    int stepL, stepR;

    if (samples <= 0)
        return;

    /* 1) Discontinuity-triggered boundary cross-fade.  Both weights are convex
     *    (sum == 256) so the result is always within the input range - no
     *    clipping and no overflow. */
    stepL = (int)L[0] - (int)declickLastL;
    stepR = (int)R[0] - (int)declickLastR;
    if (stepL > DECLICK_STEP || stepL < -DECLICK_STEP) {
        for (i = 0; i < DECLICK_RAMP && i < samples; i++) {
            int w = (i * 256) / DECLICK_RAMP;       /* 0 .. 224 */
            L[i] = (s16)(((int)declickLastL * (256 - w) + (int)L[i] * w) >> 8);
        }
    }
    if (stepR > DECLICK_STEP || stepR < -DECLICK_STEP) {
        for (i = 0; i < DECLICK_RAMP && i < samples; i++) {
            int w = (i * 256) / DECLICK_RAMP;       /* 0 .. 224 */
            R[i] = (s16)(((int)declickLastR * (256 - w) + (int)R[i] * w) >> 8);
        }
    }

    /* 2) Explicit fade to silence (transition): scale the whole panel. */
    if (dspDeclickFade) {
        int k = dspDeclickFade;                     /* DECLICK_RAMP .. 1 */
        for (i = 0; i < samples; i++) {
            L[i] = (s16)(((int)L[i] * k) / DECLICK_RAMP);
            R[i] = (s16)(((int)R[i] * k) / DECLICK_RAMP);
        }
        dspDeclickFade--;
    }

    declickLastL = L[samples - 1];
    declickLastR = R[samples - 1];
}
