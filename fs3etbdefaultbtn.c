/*
 * fs3etbdefaultbtn.c -- see fs3etbdefaultbtn.h for why these exist.
 *
 * Close/iconify/altpos/depth are sysiclass images (see the header); the
 * pixel tables below are only the fallback for whichever of those can't
 * be built (iconify before V47) plus the deck row glyphs.
 *
 * Each glyph is a small pixel table (6x6 title row, 10x10 deck row) in the
 * same raw format as EmojiGear/closebutton.c: 2-byte width, 2-byte height
 * (big-endian UWORDs), followed by width*height pen indices. The title
 * row ones are penmap.image renders, the deck ones are drawn into a
 * bitmap.image sheet (see FS3ETBDefaultBtn_CreateDeck). Pen 0 is
 * transparent; pen 1 is dark (the
 * normal glyph, and the outline of the selected deck glyphs), pen 2 white
 * (selected), pens 3 red / 4 green (deck palette only: LED, play-mode).
 */

#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/classusr.h>
#include <intuition/imageclass.h>
#include <intuition/intuitionbase.h>
#include <images/penmap.h>
#include <images/bitmap.h>
#include <graphics/gfx.h>
#include <graphics/gfxmacros.h>
#include <string.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/bitmap.h>
#include <proto/intuition.h>
#include <proto/penmap.h>
#include <utility/tagitem.h>

#include "fs3etbdefaultbtn.h"

extern struct Library *PenMapBase;
extern struct Library *BitMapBase;

/* V47 (OS 3.2) -- not in older NDKs. */
#ifndef ICONIFYIMAGE
#define ICONIFYIMAGE (0x16L)
#endif

/* PENMAP_Palette: pen count followed by that many RGB32 triplets (pen 0 is
 * implicit/transparent, not listed here -- see PENMAP_Transparent). RGB32
 * components are LEFT-justified (0xFFFFFFFF = full intensity), the
 * ObtainBestPen()/SetRGB32() convention -- penmap.image maps each entry to
 * the closest screen pen itself. */
static const ULONG tbGlyphPalette[] = {
    2,
    0x00000000, 0x00000000, 0x00000000, /* 1: glyph, normal   -- black */
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, /* 2: glyph, selected -- white */
};

/* Deck row (LED/scroll-up/play-mode): same pens 1/2, plus 3 = red
 * (network LED on, play-mode off) and 4 = green (play-mode on). Selected glyphs are white (or red) filled
 * with a pen-1 dark outline so they read on light and dark bars alike. */
static const ULONG deckGlyphPalette[] = {
    4,
    0x00000000, 0x00000000, 0x00000000, /* 1: dark / outline */
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, /* 2: white fill     */
    0xFFFFFFFF, 0x22222222, 0x22222222, /* 3: red   -- LED on, play-mode off */
    0x22222222, 0xDDDDDDDD, 0x22222222, /* 4: green -- play-mode on          */
};

#define TBGLYPH_HDR 0,6, 0,6

/* close: X */
static const UBYTE closeNormalData[] = {
    TBGLYPH_HDR,
    1,0,0,0,0,1,
    0,1,0,0,1,0,
    0,0,1,1,0,0,
    0,0,1,1,0,0,
    0,1,0,0,1,0,
    1,0,0,0,0,1,
};
static const UBYTE closeSelectedData[] = {
    TBGLYPH_HDR,
    2,0,0,0,0,2,
    0,2,0,0,2,0,
    0,0,2,2,0,0,
    0,0,2,2,0,0,
    0,2,0,0,2,0,
    2,0,0,0,0,2,
};

/* iconify: underscore bar */
static const UBYTE iconifyNormalData[] = {
    TBGLYPH_HDR,
    0,0,0,0,0,0,
    0,0,0,0,0,0,
    0,0,0,0,0,0,
    0,0,0,0,0,0,
    0,0,0,0,0,0,
    1,1,1,1,1,1,
};
static const UBYTE iconifySelectedData[] = {
    TBGLYPH_HDR,
    0,0,0,0,0,0,
    0,0,0,0,0,0,
    0,0,0,0,0,0,
    0,0,0,0,0,0,
    0,0,0,0,0,0,
    2,2,2,2,2,2,
};

/* altpos: diagonal double-headed arrow (move to alternate position) */
static const UBYTE altposNormalData[] = {
    TBGLYPH_HDR,
    1,1,0,0,0,0,
    1,0,1,0,0,0,
    0,1,0,1,0,0,
    0,0,1,0,1,0,
    0,0,0,1,0,1,
    0,0,0,0,1,1,
};
static const UBYTE altposSelectedData[] = {
    TBGLYPH_HDR,
    2,2,0,0,0,0,
    2,0,2,0,0,0,
    0,2,0,2,0,0,
    0,0,2,0,2,0,
    0,0,0,2,0,2,
    0,0,0,0,2,2,
};

/* depth: two overlapping squares (window depth arrange) */
static const UBYTE depthNormalData[] = {
    TBGLYPH_HDR,
    1,1,1,0,0,0,
    1,0,1,0,0,0,
    1,1,1,1,1,0,
    0,0,1,0,1,0,
    0,0,1,1,1,0,
    0,0,0,0,0,0,
};
static const UBYTE depthSelectedData[] = {
    TBGLYPH_HDR,
    2,2,2,0,0,0,
    2,0,2,0,0,0,
    2,2,2,2,2,0,
    0,0,2,0,2,0,
    0,0,2,2,2,0,
    0,0,0,0,0,0,
};

typedef struct {
    const UBYTE *normal;
    const UBYTE *selected;
} FS3ETBGlyph;

/* Same order as GID_TITLEBAR_CLOSE/ICONIFY/ALTPOS/DEPTH -- see
 * FS3ESTYLE_TBBUTTON_COUNT and FS3EStyle_SyncTitleBarButtons. */
static const FS3ETBGlyph tbGlyphs[FS3ESTYLE_TBBUTTON_COUNT] = {
    { closeNormalData,   closeSelectedData },
    { iconifyNormalData, iconifySelectedData },
    { altposNormalData,  altposSelectedData },
    { depthNormalData,   depthSelectedData },
};

/* sysiclass SYSIA_Which per slot, same order as tbGlyphs[]. */
static const ULONG tbSysImageWhich[FS3ESTYLE_TBBUTTON_COUNT] = {
    CLOSEIMAGE, ICONIFYIMAGE, ZOOMIMAGE, DEPTHIMAGE
};

void FS3ETBDefaultBtn_Create(FS3EStyle *st, struct Screen *scr)
{
    struct Image **out;
    int i;

    if (!st || !scr) return;
    out = st->tbDefaultImages;

#ifdef FS3E_TB_SYSIMAGES
    if (!st->tbDefaultDri) {
        st->tbDefaultDri    = GetScreenDrawInfo(scr);
        st->tbDefaultScreen = scr;
    }
#endif

    for (i = 0; i < FS3ESTYLE_TBBUTTON_COUNT; i++) {
        if (out[i]) continue; /* already built -- persistent, keep it */

#ifdef FS3E_TB_SYSIMAGES
        /* OS window-border image. Same size class Intuition picks for
         * window borders on this screen; height follows the screen font. */
        if (st->tbDefaultDri &&
            (tbSysImageWhich[i] != ICONIFYIMAGE ||
             IntuitionBase->LibNode.lib_Version >= 47))
        {
            out[i] = (struct Image *)NewObject(NULL, SYSICLASS,
                SYSIA_DrawInfo, (ULONG)st->tbDefaultDri,
                SYSIA_Which,    tbSysImageWhich[i],
                SYSIA_Size,     (ULONG)((scr->Flags & SCREENHIRES)
                                        ? SYSISIZE_MEDRES : SYSISIZE_LOWRES),
                TAG_END);
        }
#endif

        if (!out[i] && PenMapBase) {
            out[i] = (struct Image *)NewObject(PENMAP_GetClass(), NULL,
                PENMAP_RenderData,  (ULONG)tbGlyphs[i].normal,
                PENMAP_SelectData,  (ULONG)tbGlyphs[i].selected,
                PENMAP_Palette,     (ULONG)tbGlyphPalette,
                PENMAP_Screen,      (ULONG)scr,
                PENMAP_ColorMap,    (ULONG)scr->ViewPort.ColorMap,
                PENMAP_Transparent, TRUE,
                PENMAP_MaskBlit,    TRUE,
                TAG_END);
        }
    }

    /* Left at 0 without FS3E_TB_SYSIMAGES: the 6x6 penmap glyphs then sit
     * in TitleBarLayout's dpiHeight square cell, as before. */
    st->tbDefaultButtonWidth  = 0;
    st->tbDefaultButtonHeight = 0;
#ifdef FS3E_TB_SYSIMAGES
    for (i = 0; i < FS3ESTYLE_TBBUTTON_COUNT; i++) {
        if (!out[i]) continue;
        if (out[i]->Width  > st->tbDefaultButtonWidth)  st->tbDefaultButtonWidth  = out[i]->Width;
        if (out[i]->Height > st->tbDefaultButtonHeight) st->tbDefaultButtonHeight = out[i]->Height;
    }
#endif
}

void FS3ETBDefaultBtn_Dispose(FS3EStyle *st)
{
    int i;
    if (!st) return;
    for (i = 0; i < FS3ESTYLE_TBBUTTON_COUNT; i++) {
        if (st->tbDefaultImages[i]) {
            DisposeObject((Object *)st->tbDefaultImages[i]);
            st->tbDefaultImages[i] = NULL;
        }
    }
    if (st->tbDefaultDri) {
        FreeScreenDrawInfo(st->tbDefaultScreen, st->tbDefaultDri);
        st->tbDefaultDri    = NULL;
        st->tbDefaultScreen = NULL;
    }
    st->tbDefaultButtonWidth  = 0;
    st->tbDefaultButtonHeight = 0;
}

/* Deck glyphs are 10x10, 4 pixels bigger than the 6x6 title-row ones.
 * Square pixels assumed (no hires 2:1 compensation). Selected renders are
 * the same shape with a pen-1 outline (generated from the normal masks). */
#define DECKGLYPH_HDR 0,10, 0,10

/* playMode: play/pause toggle -- green right-pointing "play" triangle in
 * the normal render, red square when GA_Selected (fs3eaction.c's playing
 * state shows the selected render as "stop"). Arrays below are listed
 * selected-first. */
static const UBYTE playModeSelectedData[] = {
    DECKGLYPH_HDR,
    0,0,0,0,0,0,0,0,0,0,
    0,1,1,1,1,1,1,1,1,0,
    0,1,3,3,3,3,3,3,1,0,
    0,1,3,3,3,3,3,3,1,0,
    0,1,3,3,3,3,3,3,1,0,
    0,1,3,3,3,3,3,3,1,0,
    0,1,3,3,3,3,3,3,1,0,
    0,1,3,3,3,3,3,3,1,0,
    0,1,1,1,1,1,1,1,1,0,
    0,0,0,0,0,0,0,0,0,0,
};
static const UBYTE playModeNormalData[] = {
    DECKGLYPH_HDR,
    0,1,0,0,0,0,0,0,0,0,
    0,1,1,1,0,0,0,0,0,0,
    0,1,4,4,1,1,0,0,0,0,
    0,1,4,4,4,4,1,1,0,0,
    0,1,4,4,4,4,4,4,1,0,
    0,1,4,4,4,4,4,4,1,0,
    0,1,4,4,4,4,1,1,0,0,
    0,1,4,4,1,1,0,0,0,0,
    0,1,1,1,0,0,0,0,0,0,
    0,1,0,0,0,0,0,0,0,0,
};

/* scrollUp: an upward arrow ("scroll to top") */
static const UBYTE scrollUpNormalData[] = {
    DECKGLYPH_HDR,
    0,0,0,0,1,1,0,0,0,0,
    0,0,0,1,1,1,1,0,0,0,
    0,0,1,1,1,1,1,1,0,0,
    0,1,1,1,1,1,1,1,1,0,
    1,1,1,1,1,1,1,1,1,1,
    0,0,0,1,1,1,1,0,0,0,
    0,0,0,1,1,1,1,0,0,0,
    0,0,0,1,1,1,1,0,0,0,
    0,0,0,1,1,1,1,0,0,0,
    0,0,0,1,1,1,1,0,0,0,
};
static const UBYTE scrollUpSelectedData[] = {
    DECKGLYPH_HDR,
    0,0,0,0,1,1,0,0,0,0,
    0,0,0,1,2,2,1,0,0,0,
    0,0,1,2,2,2,2,1,0,0,
    0,1,2,2,2,2,2,2,1,0,
    1,1,1,2,2,2,2,1,1,1,
    0,0,0,1,2,2,1,0,0,0,
    0,0,0,1,2,2,1,0,0,0,
    0,0,0,1,2,2,1,0,0,0,
    0,0,0,1,2,2,1,0,0,0,
    0,0,0,1,1,1,1,0,0,0,
};

/* networkLed: a round LED -- dark when off, red with a dark rim when on
 * (GA_Selected, see FS3EApp's network LED update in friendsh3ep.c) */
static const UBYTE networkLedNormalData[] = {
    DECKGLYPH_HDR,
    0,0,0,1,1,1,1,0,0,0,
    0,0,1,1,1,1,1,1,0,0,
    0,1,1,1,1,1,1,1,1,0,
    1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,
    0,1,1,1,1,1,1,1,1,0,
    0,0,1,1,1,1,1,1,0,0,
    0,0,0,1,1,1,1,0,0,0,
};
static const UBYTE networkLedSelectedData[] = {
    DECKGLYPH_HDR,
    0,0,0,1,1,1,1,0,0,0,
    0,0,1,3,3,3,3,1,0,0,
    0,1,3,3,3,3,3,3,1,0,
    1,3,3,3,3,3,3,3,3,1,
    1,3,3,3,3,3,3,3,3,1,
    1,3,3,3,3,3,3,3,3,1,
    1,3,3,3,3,3,3,3,3,1,
    0,1,3,3,3,3,3,3,1,0,
    0,0,1,3,3,3,3,1,0,0,
    0,0,0,1,1,1,1,0,0,0,
};

/* Same order as FS3ESTYLE_BTDECK_PLAYMODE/SCROLLUP/NETWORKLED -- see
 * FS3ESTYLE_BTDECK_COUNT and FS3EStyle_SyncTitleBarDeckButtons. */
static const FS3ETBGlyph tbDeckGlyphs[FS3ESTYLE_BTDECK_COUNT] = {
    { playModeNormalData,   playModeSelectedData },
    { scrollUpSelectedData, scrollUpNormalData },   /* inverted: outlined white arrow at rest, dark when pressed */
    { networkLedNormalData, networkLedSelectedData },
};

/* Sheet layout, same as a theme's btdeck.iff: 2 columns (normal |
 * selected) x FS3ESTYLE_BTDECK_COUNT rows of DECK_CELL x DECK_CELL cells.
 * 64 pixels wide so a mask row is 8 bytes -- a modulo both planar (64-bit
 * fetch aligned) and RTG friend bitmaps agree on for the mask blit. */
#define DECK_CELL     10
#define DECK_SHEET_W  64
#define DECK_SHEET_H  (DECK_CELL * FS3ESTYLE_BTDECK_COUNT)

static void deck_free_sheet(FS3EStyle *st)
{
    int i;

    if (st->btDeckDefaultCM) {
        for (i = 0; i < 5; i++)
            if (st->btDeckDefaultPens[i] >= 0)
                ReleasePen(st->btDeckDefaultCM, (ULONG)st->btDeckDefaultPens[i]);
        st->btDeckDefaultCM = NULL;
    }
    if (st->btDeckDefaultMask) {
        FreeRaster(st->btDeckDefaultMask, DECK_SHEET_W, DECK_SHEET_H);
        st->btDeckDefaultMask = NULL;
    }
    if (st->btDeckDefaultBitMap) {
        FreeBitMap(st->btDeckDefaultBitMap);
        st->btDeckDefaultBitMap = NULL;
    }
}

void FS3ETBDefaultBtn_CreateDeck(FS3EStyle *st, struct Screen *scr)
{
    struct Image  **out;
    struct RastPort rp;
    ULONG           depth, rowBytes, i;
    int             state;

    if (!st || !scr || !BitMapBase) return;
    if (st->btDeckDefaultBitMap) return; /* already built -- persistent, keep it */
    out = st->btDeckDefaultImages;

    for (i = 0; i < 5; i++) st->btDeckDefaultPens[i] = -1;

    depth = GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH);
    st->btDeckDefaultBitMap = AllocBitMap(DECK_SHEET_W, DECK_SHEET_H, depth,
                                          BMF_CLEAR, scr->RastPort.BitMap);
    st->btDeckDefaultMask   = AllocRaster(DECK_SHEET_W, DECK_SHEET_H);
    if (!st->btDeckDefaultBitMap || !st->btDeckDefaultMask) {
        deck_free_sheet(st);
        return;
    }
    memset(st->btDeckDefaultMask, 0, RASSIZE(DECK_SHEET_W, DECK_SHEET_H));
    rowBytes = RASSIZE(DECK_SHEET_W, 1);

    /* Closest screen pen per palette entry (pen 0 stays transparent). A
     * pen ObtainBestPen() can't provide just leaves those pixels out. */
    st->btDeckDefaultCM = scr->ViewPort.ColorMap;
    for (i = 1; i <= deckGlyphPalette[0] && i < 5; i++) {
        const ULONG *rgb = &deckGlyphPalette[1 + (i - 1) * 3];
        st->btDeckDefaultPens[i] = ObtainBestPen(st->btDeckDefaultCM,
                                       rgb[0], rgb[1], rgb[2],
                                       OBP_Precision, PRECISION_IMAGE,
                                       TAG_END);
    }

    InitRastPort(&rp);
    rp.BitMap = st->btDeckDefaultBitMap;

    for (i = 0; i < FS3ESTYLE_BTDECK_COUNT; i++) {
        for (state = 0; state < 2; state++) {
            const UBYTE *data = state ? tbDeckGlyphs[i].selected : tbDeckGlyphs[i].normal;
            UWORD        w    = (UWORD)((data[0] << 8) | data[1]);
            UWORD        h    = (UWORD)((data[2] << 8) | data[3]);
            const UBYTE *px   = data + 4;
            WORD         ox   = (WORD)(state * DECK_CELL);
            WORD         oy   = (WORD)(i * DECK_CELL);
            UWORD        x, y;

            for (y = 0; y < h && y < DECK_CELL; y++) {
                for (x = 0; x < w && x < DECK_CELL; x++) {
                    UBYTE pen = px[y * w + x];
                    WORD  sx  = (WORD)(ox + x), sy = (WORD)(oy + y);

                    if (pen == 0 || pen >= 5 || st->btDeckDefaultPens[pen] < 0) continue;
                    SetAPen(&rp, (ULONG)st->btDeckDefaultPens[pen]);
                    WritePixel(&rp, sx, sy);
                    st->btDeckDefaultMask[sy * rowBytes + (sx >> 3)] |= (UBYTE)(0x80 >> (sx & 7));
                }
            }
        }
    }

    /* Same tags as the theme's btDeckImages[] (fs3estyle.c), one shared
     * mask plane addressed through the per-state offsets. */
    for (i = 0; i < FS3ESTYLE_BTDECK_COUNT; i++) {
        out[i] = (struct Image *)NewObject(BITMAP_GetClass(), NULL,
            BITMAP_BitMap,          (ULONG)st->btDeckDefaultBitMap,
            BITMAP_Width,           DECK_CELL,
            BITMAP_Height,          DECK_CELL,
            BITMAP_OffsetX,         0,
            BITMAP_OffsetY,         i * DECK_CELL,
            BITMAP_MaskPlane,       (ULONG)st->btDeckDefaultMask,
            BITMAP_SelectBitMap,    (ULONG)st->btDeckDefaultBitMap,
            BITMAP_SelectWidth,     DECK_CELL,
            BITMAP_SelectHeight,    DECK_CELL,
            BITMAP_SelectOffsetX,   DECK_CELL,
            BITMAP_SelectOffsetY,   i * DECK_CELL,
            BITMAP_SelectMaskPlane, (ULONG)st->btDeckDefaultMask,
            BITMAP_Masking,         TRUE,
            TAG_DONE);
    }
}

void FS3ETBDefaultBtn_DisposeDeck(FS3EStyle *st)
{
    int i;
    if (!st) return;
    for (i = 0; i < FS3ESTYLE_BTDECK_COUNT; i++) {
        if (st->btDeckDefaultImages[i]) {
            DisposeObject((Object *)st->btDeckDefaultImages[i]);
            st->btDeckDefaultImages[i] = NULL;
        }
    }
    deck_free_sheet(st);
}
