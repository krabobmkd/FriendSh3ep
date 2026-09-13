#ifndef FS3EBLURHASH_H
#define FS3EBLURHASH_H

/*
 * fs3eblurhash.h - integer-only BlurHash decoder for FriendSh3ep.
 *
 * $VER: fs3eblurhash.h 1.0 (13.09.2026)
 * Copyright (C) 2026 FriendSh3ep contributors. All rights reserved.
 *
 * Mastodon's media_attachments[].blurhash is a base83-encoded compact
 * representation of a DCT-like reconstruction (one DC + up to 8x8 AC
 * cosine-basis coefficients per RGB channel) meant to be evaluated at
 * every destination pixel for a smooth blurred placeholder. Doing that
 * properly needs float trig/pow per pixel, which this project avoids
 * even for its hot per-pixel bilinear scaler (see
 * scalepixelarraybilinear.h) -- and this isn't even hot, so instead of
 * a full per-pixel reconstruction, FS3EBlurHash_Decode4x4() evaluates
 * the same reconstruction formula at just the 16 cell-centers of a 4x4
 * grid and returns one flat RGB color per cell: cheap "big rectangles"
 * mosaic instead of a smooth blur, good enough for a pre-download
 * placeholder tile.
 *
 * No float/double and no libm anywhere (this project links no -lm):
 * the cosine basis values for the fixed 4x4 sample grid and the sRGB<->
 * linear-light gamma curve are both precomputed, compile-time constant
 * lookup tables (see the .c file) -- decoding is plain integer
 * fixed-point (Q12) multiply/shift/add plus a couple of per-component
 * divisions (there are at most 81 components, decoded once per
 * attachment when its toot is parsed, not per frame -- ordinary integer
 * division is fine there, unlike in a per-pixel loop).
 */

#include <exec/types.h>

#define FS3EBLURHASH_GRID  4                          /* 4x4 cells    */
#define FS3EBLURHASH_CELLS (FS3EBLURHASH_GRID * FS3EBLURHASH_GRID) /* 16 */

/*
 * Decodes hash (a NUL-terminated blurhash string, e.g. from a Mastodon
 * media_attachments[].blurhash JSON field) into 16 flat RGB colors, one
 * per cell of a 4x4 grid over the image, row-major (colors[row*4+col],
 * row 0 = top, col 0 = left) -- ready to fill 4 equal-sized rectangles
 * per row as a coarse placeholder while the real thumbnail hasn't been
 * fetched/clicked yet (see fs3esettings.h's thumbnailsOnlyOnClick).
 *
 * Returns FALSE, leaving colors untouched, if hash is NULL/empty,
 * contains a character outside the base83 alphabet, or its length
 * doesn't match the component count its own header byte declares
 * (a corrupt or non-blurhash string) -- callers should just skip the
 * placeholder in that case, same as a failed thumbnail decode.
 */
BOOL FS3EBlurHash_Decode4x4(const char *hash, UBYTE colors[FS3EBLURHASH_CELLS][3]);

#endif /* FS3EBLURHASH_H */
