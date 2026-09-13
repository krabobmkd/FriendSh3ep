/*
 * fs3eblurhash.c - integer-only BlurHash decoder for FriendSh3ep.
 *
 * $VER: fs3eblurhash.c 1.0 (13.09.2026)
 * Copyright (C) 2026 FriendSh3ep contributors. All rights reserved.
 *
 * See fs3eblurhash.h for the public contract and the "why no float/libm,
 * why a 4x4 grid instead of per-pixel" rationale. Implementation notes:
 *
 *   - fs3eBlurHashBasis[i][k] is cos(PI*i*(2k+1)/8) precomputed as Q12
 *     fixed-point (scale 4096 = 1.0), for component index i=0..8 (the
 *     format allows up to 9 components per axis) and sample point index
 *     k=0..3 (the 4 cell-centers of our fixed 4x4 grid, at x=(2k+1)/8 of
 *     the image width/height -- BlurHash's basis functions are defined
 *     purely in terms of that 0..1 fraction, not pixel counts, so the
 *     same table serves both the X and Y axis). Generated offline (see
 *     the tool comment above the table) -- these are universal
 *     constants, not per-hash data, exactly like a rounding LUT.
 *
 *   - fs3eBlurHashSrgbToLinear[256]/fs3eBlurHashLinearToSrgb[257] replace
 *     the format's pow(x,2.4)/pow(x,1/2.4) gamma correction: the first is
 *     indexed directly by a raw 0-255 sRGB byte (DC only decodes to
 *     that), the second by a Q12 linear sum right-shifted by 4 (i.e. its
 *     top 8 bits, since sums are clamped to 0..4096 before lookup).
 *
 *   - decode83() is the only per-hash-string parsing step; everything
 *     after it (AC/DC unpacking, the 16-cell reconstruction) is
 *     multiply/shift/add on LONGs, plus one integer divide per AC
 *     component (at most 80 of them, decoded once per attachment when
 *     its toot is parsed -- not a per-pixel or per-frame cost, so a
 *     plain '/' is fine here unlike in a hot loop).
 */

#include "fs3eblurhash.h"
#include <string.h>

#define FS3EBH_MAX_COMPONENTS 81 /* 9x9, the format's per-axis maximum */

/* Base83 alphabet reverse lookup: fs3eBlurHashBase83Value[(UBYTE)c] is
 * that character's 0-82 digit value, or 0xFF if c isn't part of the
 * BlurHash base83 alphabet ("0-9A-Za-z#$%*+,-.:;=?@[]^_{|}~"). */
static const UBYTE fs3eBlurHashBase83Value[256] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x3E, 0x3F, 0x40, 0xFF, 0xFF, 0xFF, 0xFF, 0x41, 0x42, 0x43, 0x44, 0x45, 0xFF,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x46, 0x47, 0xFF, 0x48, 0xFF, 0x49,
    0x4A, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x4B, 0xFF, 0x4C, 0x4D, 0x4E,
    0xFF, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32,
    0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x4F, 0x50, 0x51, 0x52, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

/* cos(PI*i*(2k+1)/8) * 4096, i=0..8 (component index), k=0..3 (this
 * project's fixed 4x4 sample-grid cell centers). Offline-generated:
 *   for i in range(9):
 *       for k in range(4):
 *           round(cos(pi*i*(2*k+1)/8) * 4096)
 */
static const WORD fs3eBlurHashBasis[9][4] = {
    {   4096,   4096,   4096,   4096 },
    {   3784,   1567,  -1567,  -3784 },
    {   2896,  -2896,  -2896,   2896 },
    {   1567,  -3784,   3784,  -1567 },
    {      0,      0,      0,      0 },
    {  -1567,   3784,  -3784,   1567 },
    {  -2896,   2896,   2896,  -2896 },
    {  -3784,  -1567,   1567,   3784 },
    {  -4096,  -4096,  -4096,  -4096 },
};

/* sRGB byte (0-255) -> linear-light Q12 (0-4096). Offline-generated
 * standard sRGB EOTF, quantized to Q12 and rounded. */
static const UWORD fs3eBlurHashSrgbToLinear[256] = {
        0,     1,     2,     4,     5,     6,     7,     9,
       10,    11,    12,    14,    15,    16,    18,    20,
       21,    23,    25,    27,    29,    31,    33,    35,
       37,    40,    42,    45,    48,    50,    53,    56,
       59,    62,    66,    69,    72,    76,    79,    83,
       87,    91,    95,    99,   103,   107,   112,   116,
      121,   126,   131,   136,   141,   146,   151,   156,
      162,   168,   173,   179,   185,   191,   197,   204,
      210,   217,   223,   230,   237,   244,   251,   258,
      265,   273,   280,   288,   296,   304,   312,   320,
      329,   337,   346,   354,   363,   372,   381,   390,
      400,   409,   419,   429,   438,   448,   458,   469,
      479,   490,   500,   511,   522,   533,   544,   556,
      567,   579,   590,   602,   614,   626,   639,   651,
      664,   676,   689,   702,   715,   729,   742,   756,
      769,   783,   797,   811,   826,   840,   855,   869,
      884,   899,   914,   930,   945,   961,   976,   992,
     1008,  1025,  1041,  1058,  1074,  1091,  1108,  1125,
     1142,  1160,  1177,  1195,  1213,  1231,  1249,  1268,
     1286,  1305,  1324,  1343,  1362,  1381,  1400,  1420,
     1440,  1460,  1480,  1500,  1521,  1541,  1562,  1583,
     1604,  1625,  1647,  1668,  1690,  1712,  1734,  1756,
     1778,  1801,  1824,  1846,  1869,  1893,  1916,  1940,
     1963,  1987,  2011,  2035,  2060,  2084,  2109,  2134,
     2159,  2184,  2210,  2235,  2261,  2287,  2313,  2339,
     2366,  2392,  2419,  2446,  2473,  2501,  2528,  2556,
     2584,  2612,  2640,  2668,  2697,  2725,  2754,  2783,
     2813,  2842,  2872,  2902,  2931,  2962,  2992,  3022,
     3053,  3084,  3115,  3146,  3178,  3209,  3241,  3273,
     3305,  3338,  3370,  3403,  3436,  3469,  3502,  3535,
     3569,  3603,  3637,  3671,  3705,  3740,  3775,  3810,
     3845,  3880,  3916,  3951,  3987,  4023,  4060,  4096,
};

/* Inverse of the above: index by a Q12 linear-light sum clamped to
 * 0..4096 and right-shifted by 4 (top 8 bits, 0-256) -> sRGB byte. */
static const UBYTE fs3eBlurHashLinearToSrgb[257] = {
      0,  13,  22,  28,  34,  38,  42,  46,  49,  53,  56,  58,  61,  64,  66,  68,
     71,  73,  75,  77,  79,  81,  83,  85,  86,  88,  90,  91,  93,  95,  96,  98,
     99, 101, 102, 103, 105, 106, 107, 109, 110, 111, 113, 114, 115, 116, 118, 119,
    120, 121, 122, 123, 124, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136,
    137, 138, 139, 140, 141, 142, 143, 144, 145, 145, 146, 147, 148, 149, 150, 151,
    152, 153, 153, 154, 155, 156, 157, 158, 158, 159, 160, 161, 162, 162, 163, 164,
    165, 166, 166, 167, 168, 169, 169, 170, 171, 172, 172, 173, 174, 174, 175, 176,
    177, 177, 178, 179, 179, 180, 181, 181, 182, 183, 184, 184, 185, 186, 186, 187,
    188, 188, 189, 189, 190, 191, 191, 192, 193, 193, 194, 195, 195, 196, 196, 197,
    198, 198, 199, 199, 200, 201, 201, 202, 202, 203, 204, 204, 205, 205, 206, 207,
    207, 208, 208, 209, 209, 210, 211, 211, 212, 212, 213, 213, 214, 214, 215, 216,
    216, 217, 217, 218, 218, 219, 219, 220, 220, 221, 221, 222, 223, 223, 224, 224,
    225, 225, 226, 226, 227, 227, 228, 228, 229, 229, 230, 230, 231, 231, 232, 232,
    233, 233, 234, 234, 235, 235, 236, 236, 237, 237, 238, 238, 239, 239, 239, 240,
    240, 241, 241, 242, 242, 243, 243, 244, 244, 245, 245, 246, 246, 246, 247, 247,
    248, 248, 249, 249, 250, 250, 251, 251, 251, 252, 252, 253, 253, 254, 254, 255,
    255,
};

/* decode83() over hash[offset..offset+len) -- returns FALSE (value
 * undefined) if any character in that span isn't in the base83
 * alphabet. Caller has already range-checked offset+len against the
 * hash's total length. */
static BOOL decode83(const char *hash, int offset, int len, LONG *value)
{
    LONG v = 0;
    int  i;

    for (i = 0; i < len; i++) {
        UBYTE digit = fs3eBlurHashBase83Value[(UBYTE)hash[offset + i]];
        if (digit == 0xFF) return FALSE;
        v = v * 83 + digit;
    }
    *value = v;
    return TRUE;
}

/* signPow(v,2) for v=(quant-9)/9, scaled to Q12 and pre-multiplied by
 * maxValueFixed (also Q12) -- see fs3eblurhash.h's doc comment for why
 * exponent 2 needs no pow(): it's just a sign-preserving square. */
static LONG decodeAcChannel(LONG quant, LONG maxValueFixed)
{
    LONG num = quant - 9;         /* -9..9 */
    LONG sq  = num * num;         /* 0..81 */
    LONG mag = (sq * 4096L) / 81; /* Q12, 0..4096 */
    LONG ch  = (num < 0) ? -mag : mag;
    return (ch * maxValueFixed) >> 12; /* Q12 */
}

BOOL FS3EBlurHash_Decode4x4(const char *hash, UBYTE colors[FS3EBLURHASH_CELLS][3])
{
    int  len, headerLen, expectedLen, components;
    LONG sizeFlag, numX, numY;
    LONG quantMax, maxValueFixed = 0;
    LONG dcValue;
    LONG compFixed[FS3EBH_MAX_COMPONENTS][3];
    int  t, i, j, r, c;

    if (!hash || !hash[0]) return FALSE;
    len = strlen(hash);

    if (!decode83(hash, 0, 1, &sizeFlag)) return FALSE;
    numY = sizeFlag / 9 + 1;
    numX = sizeFlag % 9 + 1;
    if (numX < 1 || numX > 9 || numY < 1 || numY > 9) return FALSE;
    components = (int)(numX * numY);

    headerLen   = (components > 1) ? 2 : 1;
    expectedLen = headerLen + 4 + (components - 1) * 2;
    if (len != expectedLen) return FALSE;

    if (components > 1) {
        if (!decode83(hash, 1, 1, &quantMax)) return FALSE;
        maxValueFixed = ((quantMax + 1) * 4096L) / 166;
    }

    if (!decode83(hash, headerLen, 4, &dcValue)) return FALSE;
    compFixed[0][0] = fs3eBlurHashSrgbToLinear[(dcValue >> 16) & 0xFF];
    compFixed[0][1] = fs3eBlurHashSrgbToLinear[(dcValue >> 8)  & 0xFF];
    compFixed[0][2] = fs3eBlurHashSrgbToLinear[dcValue & 0xFF];

    for (t = 1; t < components; t++) {
        LONG acValue, quantR, quantG, quantB;
        int  offset = headerLen + 4 + (t - 1) * 2;

        if (!decode83(hash, offset, 2, &acValue)) return FALSE;
        quantR = acValue / (19 * 19);
        quantG = (acValue / 19) % 19;
        quantB = acValue % 19;

        compFixed[t][0] = decodeAcChannel(quantR, maxValueFixed);
        compFixed[t][1] = decodeAcChannel(quantG, maxValueFixed);
        compFixed[t][2] = decodeAcChannel(quantB, maxValueFixed);
    }

    for (r = 0; r < FS3EBLURHASH_GRID; r++) {
        for (c = 0; c < FS3EBLURHASH_GRID; c++) {
            LONG sum[3] = { 0, 0, 0 };

            for (j = 0; j < numY; j++) {
                for (i = 0; i < numX; i++) {
                    int  comp  = j * (int)numX + i;
                    LONG basis = ((LONG)fs3eBlurHashBasis[i][c] *
                                  (LONG)fs3eBlurHashBasis[j][r]) >> 12;

                    sum[0] += (compFixed[comp][0] * basis) >> 12;
                    sum[1] += (compFixed[comp][1] * basis) >> 12;
                    sum[2] += (compFixed[comp][2] * basis) >> 12;
                }
            }

            for (i = 0; i < 3; i++) {
                LONG v = sum[i];
                if (v < 0)    v = 0;
                if (v > 4096) v = 4096;
                colors[r * FS3EBLURHASH_GRID + c][i] = fs3eBlurHashLinearToSrgb[v >> 4];
            }
        }
    }

    return TRUE;
}
