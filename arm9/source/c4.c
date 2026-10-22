/*******************************************************************************
  Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.
 
  (c) Copyright 1996 - 2003 Gary Henderson (gary.henderson@ntlworld.com) and
                            Jerremy Koot (jkoot@snes9x.com)

  (c) Copyright 2002 - 2003 Matthew Kendora and
                            Brad Jorsch (anomie@users.sourceforge.net)
 

                      
  C4 x86 assembler and some C emulation code
  (c) Copyright 2000 - 2003 zsKnight (zsknight@zsnes.com),
                            _Demo_ (_demo_@zsnes.com), and
                            Nach (n-a-c-h@users.sourceforge.net)
                                          
  C4 C++ code
  (c) Copyright 2003 Brad Jorsch

  DSP-1 emulator code
  (c) Copyright 1998 - 2003 Ivar (ivar@snes9x.com), _Demo_, Gary Henderson,
                            John Weidman (jweidman@slip.net),
                            neviksti (neviksti@hotmail.com), and
                            Kris Bleakley (stinkfish@bigpond.com)
 
  DSP-2 emulator code
  (c) Copyright 2003 Kris Bleakley, John Weidman, neviksti, Matthew Kendora, and
                     Lord Nightmare (lord_nightmare@users.sourceforge.net

  OBC1 emulator code
  (c) Copyright 2001 - 2003 zsKnight, pagefault (pagefault@zsnes.com)
  Ported from x86 assembler to C by sanmaiwashi

  SPC7110 and RTC C++ emulator code
  (c) Copyright 2002 Matthew Kendora with research by
                     zsKnight, John Weidman, and Dark Force

  S-RTC C emulator code
  (c) Copyright 2001 John Weidman
  
  Super FX x86 assembler emulator code 
  (c) Copyright 1998 - 2003 zsKnight, _Demo_, and pagefault 

  Super FX C emulator code 
  (c) Copyright 1997 - 1999 Ivar and Gary Henderson.



 
  Specific ports contains the works of other authors. See headers in
  individual files.
 
  Snes9x homepage: http://www.snes9x.com
 
  Permission to use, copy, modify and distribute Snes9x in both binary and
  source form, for non-commercial purposes, is hereby granted without fee,
  providing that this license information and copyright notice appear with
  all copies and any derived work.
 
  This software is provided 'as-is', without any express or implied
  warranty. In no event shall the authors be held liable for any damages
  arising from the use of this software.
 
  Snes9x is freeware for PERSONAL USE only. Commercial users should
  seek permission of the copyright holders first. Commercial use includes
  charging money for Snes9x or software derived from Snes9x.
 
  The copyright holders request that bug fixes and improvements to the code
  should be forwarded to them so everyone can benefit from the modifications
  in future versions.
 
  Super NES and Super Nintendo Entertainment System are trademarks of
  Nintendo Co., Limited and its subsidiary companies.
*******************************************************************************/
#include <stdlib.h>
#include "c4.h"
#include "snemulds_memmap.h"

short C4WFXVal;
short C4WFYVal;
short C4WFZVal;
short C4WFX2Val;
short C4WFY2Val;
short C4WFDist;
short C4WFScale;

/*
 * -------------------------------------------------------------------------
 * Fixed-point CX4 math (no FPU on ARM946E-S)
 * -------------------------------------------------------------------------
 * The previous implementation used double/cos/sin/atan/sqrt, forcing libgcc
 * soft-float on every CX4 vector op. The C4 tables (C4SinTable/C4CosTable in
 * c4emu.c) are Q1.15 (32767 == 1.0) over a 512-step revolution, and the rest
 * of the C4 code already consumes them with a 15-bit shift (see
 * C4DoScaleRotate, and the $10/$13 handlers). This file keeps that same
 * domain so all C4 math stays integer:
 *
 *   C4_FIXED_SHIFT = 15  ->  coordinates are Q16.15 (scale 2^15)
 *   angles          = 512 steps per revolution (index = -angle * 4)
 *
 * With int16 inputs the worst-case rotated magnitude is
 * sqrt(2) * 32767 * 2^15 = 1.52e9, which fits in int32. Every product is
 * accumulated in int64 (ARMv5TE SMULL) before a single arithmetic right
 * shift, so no intermediate truncation occurs and no soft-float is emitted.
 */
#define C4_FIXED_SHIFT 15
#define C4_FIXED_ONE   (1 << C4_FIXED_SHIFT)

/* CX4 angles are in units of 1/128 revolution; table step is 1/512 -> *4 */
#define C4_ANGLE_TO_INDEX(v)  (((-(int32)(v)) * 4) & 0x1FF)

/* Deterministic integer square root (floor), bit-by-bit, no soft-float.
 * 64-bit input so callers can request fractional precision by pre-shifting. */
static uint32 c4_isqrt(int64 n)
{
    int64 res = 0;
    int64 bit = (int64)1 << 62;

    while (bit > n)
        bit >>= 2;
    while (bit) {
        if (n >= res + bit) {
            n   -= res + bit;
            res  = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return (uint32)res;
}

void C4TransfWireFrame ()
{
    int32 c4x, c4y, c4z, c4x2, c4y2, c4z2;
    int32 s, c, idx;

    c4x = (int32) C4WFXVal << C4_FIXED_SHIFT;
    c4y = (int32) C4WFYVal << C4_FIXED_SHIFT;
    c4z = ((int32) C4WFZVal - 0x95) << C4_FIXED_SHIFT;

    // Rotate X
    idx = C4_ANGLE_TO_INDEX(C4WFX2Val);
    s = C4SinTable[idx];
    c = C4CosTable[idx];
    c4y2 = (int32)((((int64)c4y * c) - ((int64)c4z * s)) >> C4_FIXED_SHIFT);
    c4z2 = (int32)((((int64)c4y * s) + ((int64)c4z * c)) >> C4_FIXED_SHIFT);

    // Rotate Y
    idx = C4_ANGLE_TO_INDEX(C4WFY2Val);
    s = C4SinTable[idx];
    c = C4CosTable[idx];
    c4x2 = (int32)((((int64)c4x * c) + ((int64)c4z2 * s)) >> C4_FIXED_SHIFT);
    c4z  = (int32)((((int64)c4z2 * c) - ((int64)c4x * s)) >> C4_FIXED_SHIFT);

    // Rotate Z
    idx = C4_ANGLE_TO_INDEX(C4WFDist);
    s = C4SinTable[idx];
    c = C4CosTable[idx];
    c4x = (int32)((((int64)c4x2 * c) - ((int64)c4y2 * s)) >> C4_FIXED_SHIFT);
    c4y = (int32)((((int64)c4x2 * s) + ((int64)c4y2 * c)) >> C4_FIXED_SHIFT);

    // Scale: out = coord * scale / (0x90 * (z + 0x95)) * 0x95.
    // The 2^15 factors of coord and (z+0x95) cancel, so the quotient is
    // already in integer units (matches the original double->short cast).
    {
        int64 zden = (int64)c4z + ((int64)0x95 << C4_FIXED_SHIFT);
        C4WFXVal = (short)(((int64)c4x * C4WFScale * 0x95) / (0x90 * zden));
        C4WFYVal = (short)(((int64)c4y * C4WFScale * 0x95) / (0x90 * zden));
    }
}

void C4TransfWireFrame2 ()
{
    int32 c4x, c4y, c4z, c4x2, c4y2, c4z2;
    int32 s, c, idx;

    c4x = (int32) C4WFXVal << C4_FIXED_SHIFT;
    c4y = (int32) C4WFYVal << C4_FIXED_SHIFT;
    c4z = (int32) C4WFZVal << C4_FIXED_SHIFT;

    // Rotate X
    idx = C4_ANGLE_TO_INDEX(C4WFX2Val);
    s = C4SinTable[idx];
    c = C4CosTable[idx];
    c4y2 = (int32)((((int64)c4y * c) - ((int64)c4z * s)) >> C4_FIXED_SHIFT);
    c4z2 = (int32)((((int64)c4y * s) + ((int64)c4z * c)) >> C4_FIXED_SHIFT);

    // Rotate Y
    idx = C4_ANGLE_TO_INDEX(C4WFY2Val);
    s = C4SinTable[idx];
    c = C4CosTable[idx];
    c4x2 = (int32)((((int64)c4x * c) + ((int64)c4z2 * s)) >> C4_FIXED_SHIFT);
    c4z  = (int32)((((int64)c4z2 * c) - ((int64)c4x * s)) >> C4_FIXED_SHIFT);

    // Rotate Z
    idx = C4_ANGLE_TO_INDEX(C4WFDist);
    s = C4SinTable[idx];
    c = C4CosTable[idx];
    c4x = (int32)((((int64)c4x2 * c) - ((int64)c4y2 * s)) >> C4_FIXED_SHIFT);
    c4y = (int32)((((int64)c4x2 * s) + ((int64)c4y2 * c)) >> C4_FIXED_SHIFT);

    // Scale: out = coord * scale / 0x100  (Q16.15 coord -> integer units)
    C4WFXVal = (short)(((int64)c4x * C4WFScale) / ((int64)0x100 << C4_FIXED_SHIFT));
    C4WFYVal = (short)(((int64)c4y * C4WFScale) / ((int64)0x100 << C4_FIXED_SHIFT));
}

void C4CalcWireFrame ()
{
    C4WFXVal = C4WFX2Val - C4WFXVal;
    C4WFYVal = C4WFY2Val - C4WFYVal;
    if (abs (C4WFXVal) > abs (C4WFYVal))
    {
        C4WFDist = abs (C4WFXVal) + 1;
        C4WFYVal = (short) ((256 * (int32) C4WFYVal) / abs (C4WFXVal));
        if (C4WFXVal < 0)
            C4WFXVal = -256;
        else 
            C4WFXVal = 256;
    }
    else
    {
        if (C4WFYVal != 0) 
        {
            C4WFDist = abs(C4WFYVal)+1;
            C4WFXVal = (short) ((256 * (int32) C4WFXVal) / abs (C4WFYVal));
            if (C4WFYVal < 0)
                C4WFYVal = -256;
            else 
                C4WFYVal = 256;
        }
        else 
            C4WFDist = 0;
    }
}

short C41FXVal;
short C41FYVal;
short C41FAngleRes;
short C41FDist;
short C41FDistVal;

/*
 * Table-based atan2 for the CX4 "atan" command (0x1f). Returns the C4 angle
 * index (0..511, 512 steps per revolution) for the vector (fx, fy) using a
 * binary search over the Q1.15 sin/cos tables, replacing atan()/division with
 * pure integer work. Accurate to one table step (2*pi/512 ~= 0.70 deg), which
 * is the CX4's native trig resolution.
 */
static int32 c4_atan2_index(int32 fx, int32 fy)
{
    int32 ax = (fx < 0) ? -fx : fx;
    int32 ay = (fy < 0) ? -fy : fy;
    int32 lo = 0, hi = 128;

    if (ax == 0)
        return (fy > 0) ? 0x80 : 0x180;

    /* first octant: find i in [0,128] where Sin[i]/Cos[i] crosses ay/ax */
    while (lo < hi) {
        int32 mid = (lo + hi) >> 1;
        if (((int64)C4SinTable[mid] * ax) >= ((int64)C4CosTable[mid] * ay))
            hi = mid;
        else
            lo = mid + 1;
    }
    /* choose the nearer of the two straddling indices */
    if (lo > 0) {
        int64 e0 = (int64)C4SinTable[lo - 1] * ax - (int64)C4CosTable[lo - 1] * ay;
        int64 e1 = (int64)C4SinTable[lo]     * ax - (int64)C4CosTable[lo]     * ay;
        if (e0 < 0) e0 = -e0;
        if (e1 < 0) e1 = -e1;
        if (e0 < e1)
            lo = lo - 1;
    }

    /* fold the first-octant angle into the correct quadrant */
    if (fx >= 0)
        return (fy >= 0) ? lo : (0x200 - lo);
    else
        return (fy >= 0) ? (0x100 - lo) : (0x100 + lo);
}

void C4Op1F ()
{
    if (C41FXVal == 0) 
    {
        if (C41FYVal > 0) 
            C41FAngleRes = 0x80;
        else 
            C41FAngleRes = 0x180;
    }
    else 
    {
        C41FAngleRes = (short) (c4_atan2_index(C41FXVal, C41FYVal) & 0x1FF);
    }
}

void C4Op15()
{
    uint32 n = (uint32)((int32)C41FXVal * (int32)C41FXVal) +
               (uint32)((int32)C41FYVal * (int32)C41FYVal);
    C41FDist = (short) c4_isqrt(n);
}

void C4Op0D()
{
    uint32 n = (uint32)((int32)C41FXVal * (int32)C41FXVal) +
               (uint32)((int32)C41FYVal * (int32)C41FYVal);
    uint32 d;

    if (n == 0)
        return;

    /* was: FY *= (C41FDistVal / sqrt(n)) * 0.99 ; FX *= (...) * 0.98.
     * Use sqrt with 8 fractional bits (d ~= sqrt(n)*256) so the reciprocal
     * keeps ~2 decimal digits, closely matching the original double path. */
    d = c4_isqrt((int64)n << 16);
    if (d == 0)
        return;

    C41FYVal = (short)(((int64)C41FYVal * C41FDistVal * 99 * 256) / ((int64)d * 100));
    C41FXVal = (short)(((int64)C41FXVal * C41FDistVal * 98 * 256) / ((int64)d * 100));
}

#ifdef ZSNES_C4
void C4LoaDMem(char *C4RAM)
{
  memmove(C4RAM+(READ_WORD(C4RAM+0x1f45)&0x1fff), 
          S9xGetMemPointer(READ_3WORD(C4RAM+0x1f40)),
          READ_WORD(C4RAM+0x1f43));
}
#endif