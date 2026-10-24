#ifndef FS3ETBDEFAULTBTN_H
#define FS3ETBDEFAULTBTN_H

/*
 * fs3etbdefaultbtn -- the built-in title bar button glyphs used whenever no
 * theme image is loaded for a given slot: 4 for the close/iconify/altpos/
 * depth row (FS3ETBDefaultBtn_Create/Dispose, st->tbDefaultImages[]) and 3
 * for the NetworkLed/ScrollUp/PlayMode "deck" row (FS3ETBDefaultBtn_
 * CreateDeck/DisposeDeck, st->btDeckDefaultImages[]) -- two independent
 * sheets, same as st->tbImages[]/st->btDeckImages[] they fall back for.
 *
 * With the FS3E_TB_SYSIMAGES CMake option (OFF by default) the
 * close/iconify/altpos/depth row uses the OS's own window-border
 * images: Intuition's sysiclass with SYSIA_Which = CLOSEIMAGE /
 * ICONIFYIMAGE / ZOOMIMAGE / DEPTHIMAGE, so "no theme" looks exactly like
 * the user's other windows (and follows their sysihack/theme patches).
 * sysiclass draws its own pressed state (IDS_SELECTED), so one image per
 * button is enough. ICONIFYIMAGE only exists from intuition V47 (OS 3.2);
 * on older Intuition -- or if any sysiclass NewObject() fails -- that
 * slot falls back to a penmap.image built from a fixed 6x6 2-color pixel
 * table baked into fs3etbdefaultbtn.c (same construction pattern as
 * EmojiGear/closebutton.c). Without the option, that row always uses the
 * penmap glyphs.
 *
 * Why these exist: button.gadget does not tolerate a live gadget going
 * from a real GA_Image back to GA_Image=NULL once one has been attached --
 * confirmed glitches/crashes switching from an image theme back to
 * "no theme". So "no theme" now means these default glyphs instead of a
 * literal NULL image, and GA_Image on any of these 7 title bar buttons
 * never goes to NULL again once FS3EStyle_CreateDefaultButtonImages() has
 * run once.
 */

#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/classusr.h>

#include "fs3estyle.h"

/* Create any not-yet-created slots of st->tbDefaultImages[] (in
 * GID_TITLEBAR_CLOSE, ICONIFY, ALTPOS, DEPTH order) for scr, keeping
 * scr's DrawInfo in st->tbDefaultDri for their lifetime, and update
 * st->tbDefaultButtonWidth/Height. Idempotent -- a slot that already
 * holds an Image* is left untouched, so this is safe to call on every
 * window (re)open, not just the first. These are meant to live for the
 * whole app run: pair with FS3ETBDefaultBtn_Dispose() only at final
 * teardown, never on a theme switch. No-op if scr is NULL. */
void FS3ETBDefaultBtn_Create(FS3EStyle *st, struct Screen *scr);

/* Dispose whatever FS3ETBDefaultBtn_Create() built, NULL every slot and
 * release the DrawInfo. Call once, at final app teardown (alongside
 * FS3EStyle_FreeThemeImages). */
void FS3ETBDefaultBtn_Dispose(FS3EStyle *st);

/* Same lifetime rules as FS3ETBDefaultBtn_Create/Dispose above, for the
 * 3-slot NetworkLed/ScrollUp/PlayMode "deck" row (st->btDeckDefaultImages[],
 * in FS3ESTYLE_BTDECK_PLAYMODE/SCROLLUP/NETWORKLED order -- see
 * FS3EStyle_SyncTitleBarDeckButtons).
 *
 * These are NOT penmap.image: penmap's PENMAP_MaskBlit builds a single
 * mask from the normal render, so a selected render with a different
 * silhouette (play/pause: triangle vs square) came out non-transparent.
 * Instead the pixel tables are drawn (with ObtainBestPen()'d screen pens)
 * into one small sheet bitmap + mask plane, laid out exactly like a
 * theme's btdeck.iff (2 columns normal|selected x 3 rows), and sliced
 * into bitmap.image objects with per-state masks -- the same, proven,
 * path theme images take. */
void FS3ETBDefaultBtn_CreateDeck(FS3EStyle *st, struct Screen *scr);
void FS3ETBDefaultBtn_DisposeDeck(FS3EStyle *st);

#endif /* FS3ETBDEFAULTBTN_H */
