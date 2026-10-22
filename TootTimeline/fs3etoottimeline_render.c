/*
 * TootTimeline – GM_LAYOUT, GM_RENDER, GM_DOMAIN.
 *
 * GM_LAYOUT / size-change detection
 * ----------------------------------
 * GM_LAYOUT (called by the parent layout gadget when the window is
 * resized) updates gadWidth/gadHeight.  If gadWidth differs from
 * lastTileWidth the tile pool is freed and rebuilt, and all posts are
 * re-laid-out so their heights reflect the new column width.
 *
 * TTLM_Prepare (main task, before RefreshGList -- see fs3etoottimeline.h)
 * ------------
 * 1. Detect size change and call ttl_do_layout if needed.
 * 2. Apply any pending scroll delta (set by TTIMELINE_ScrollY/ScrollBy).
 * 3. Evict tiles outside the warm window:
 *    [scrollY - TTL_TILE_BUF*TTL_TILE_HEIGHT,
 *     scrollY + gadHeight + TTL_TILE_BUF*TTL_TILE_HEIGHT)
 * 4. Acquire every tile of the warm window (visible ones first) and
 *    ttl_render_tile() the dirty ones -- listSem is dropped between tiles.
 *
 * Why this is not in GM_RENDER: Intuition runs GM_RENDER with the
 * window's layer locked, and while it is held input.device stalls
 * (pointer freeze on slow CPUs). Rendering a dozen tiles of FreeType text
 * and images there made every timeline refresh freeze the mouse.
 *
 * GM_RENDER
 * ---------
 * 1./2. Same as above (cheap no-op when TTLM_Prepare already ran).
 * 3. For each tile row intersecting the VISIBLE area only: acquire it,
 *    render it inline only if still dirty (fallback for refreshes
 *    Intuition/layout.gadget trigger themselves -- resize, damage), then
 *    BltBitMapRastPort to the gadget RastPort. The warm buffer rows are
 *    left to the next TTLM_Prepare.
 * 4. Fill empty space above or below all posts with bgPen.
 * 5. Draw text-selection overlay (COMPLEMENT mode) — placeholder.
 */

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/layers.h>
#include <proto/alib.h>
#include <intuition/gadgetclass.h>
#include <graphics/gfx.h>
#include <graphics/gfxmacros.h>

#include "fs3etoottimeline_private.h"

#include "../bdbprintf.h"

/* ------------------------------------------------------------------ */
/* Internal layout                                                      */
/* ------------------------------------------------------------------ */

static void ttl_do_layout(Class *cl, Object *o, WORD newW, WORD newH, struct RastPort *rp)
{
    TTLData    *inst = TTL_DATA(cl, o);
    TTLChannel *active;
    BOOL widthChanged = (newW != inst->lastTileWidth);

    inst->gadWidth  = newW;
    inst->gadHeight = newH;

    if (widthChanged || inst->tileCount == 0) {
        ttl_tiles_free(inst);
        ttl_tiles_alloc(inst,rp);
        /* Relays out every channel (not just the active one) so any of
         * them is ready to display correctly the moment it becomes
         * active -- see ttl_layout_all_posts. It already rebuilds Y
         * positions for every channel itself. */
        if (widthChanged)
            ttl_layout_all_posts(inst);
    }

    /* Clamp the active channel's scrollY to its valid range -- also runs
     * on a pure TTIMELINE_ViewMode switch (no width change), since the
     * newly active channel's extents may differ from the previous one's. */
    active = ttl_active(inst);
    {
        LONG minScroll = ttl_channel_min_scroll(active);
        LONG maxScroll = active->contentBottomY - inst->gadHeight;
        if (maxScroll < minScroll) maxScroll = minScroll;
        if (active->scrollY < minScroll) active->scrollY = minScroll;
        if (active->scrollY > maxScroll) active->scrollY = maxScroll;
    }
}

/* Relayout if the gadget size changed (or GM_LAYOUT asked for it), then
 * apply any pending scroll. Shared by TTLM_Prepare and GM_RENDER; caller
 * holds listSem. Cheap when there's nothing to do. */
static void ttl_layout_and_scroll(Class *cl, Object *o, struct RastPort *rp)
{
    TTLData       *inst = TTL_DATA(cl, o);
    struct Gadget *g    = G(o);

    if (inst->LastLayoutedWidth != g->Width ||
        inst->LastLayoutedHeight != g->Height)
    {
        inst->layoutToDo = TRUE;
    }

    if (inst->layoutToDo)
    {
        ttl_do_layout(cl, o, g->Width, g->Height, rp);

        inst->LastLayoutedWidth = g->Width;
        inst->LastLayoutedHeight = g->Height;
        inst->layoutToDo = FALSE;
    }

    if (!ttl_is_waiting(inst) && inst->pendingScroll) {
        TTLChannel *active = ttl_active(inst);
        LONG minScroll = ttl_channel_min_scroll(active);
        LONG maxScroll = active->contentBottomY - g->Height;
        if (maxScroll < minScroll) maxScroll = minScroll;
        active->scrollY = inst->pendingScrollY;
        if (active->scrollY < minScroll) active->scrollY = minScroll;
        if (active->scrollY > maxScroll) active->scrollY = maxScroll;
        inst->pendingScroll = FALSE;
    }
}

/* ------------------------------------------------------------------ */
/* TTLM_Prepare                                                         */
/* ------------------------------------------------------------------ */

ULONG TTL_OnPrepare(Class *cl, Object *o, struct ttlpPrepare *msg)
{
    TTLData       *inst = TTL_DATA(cl, o);
    struct Gadget *g    = G(o);
    struct Window *win  = msg->ttlp_Window;
    ULONG          visibleRendered = 0;
    LONG           firstIdx, lastIdx, visFirst, visLast, tileIdx;
    int            pass;

    if (!win || !win->RPort || !win->RPort->BitMap ||
        g->Width <= 0 || g->Height <= 0 || inst->style == NULL ||
        FindTask(NULL) != inst->callerTask)
        return 0;

    inst->screen = win->WScreen;

    ObtainSemaphore(&inst->listSem);

    ttl_layout_and_scroll(cl, o, win->RPort);

    if (ttl_is_waiting(inst) || inst->tileCount == 0) {
        ReleaseSemaphore(&inst->listSem);
        return 0;
    }

    {
        TTLChannel *active = ttl_active(inst);
        LONG warmTop = active->scrollY - (LONG)TTL_TILE_BUF * TTL_TILE_HEIGHT;
        LONG warmBot = active->scrollY + g->Height + (LONG)TTL_TILE_BUF * TTL_TILE_HEIGHT;

        ttl_tile_evict_out_of_range(inst, warmTop, warmBot);

        firstIdx = ttl_tile_index(warmTop);
        lastIdx  = ttl_tile_index(warmBot - 1);
        visFirst = ttl_tile_index(active->scrollY);
        visLast  = ttl_tile_index(active->scrollY + g->Height - 1);
    }

    ReleaseSemaphore(&inst->listSem);

    /* Pass 0: visible rows, pass 1: the warm buffer rows around them.
     * One tile per listSem hold, so a concurrent GM_HANDLEINPUT/GM_GOACTIVE
     * on input.device never waits for more than a single tile render. The
     * tile pool itself is only ever touched on this task (GM_RENDER off
     * callerTask defers, see TTL_OnRender), so the indices computed above
     * stay valid; a drag-scroll meanwhile just means a row or two of the
     * warm window gets prepared one refresh late. */
    for (pass = 0; pass < 2; pass++) {
        for (tileIdx = firstIdx; tileIdx <= lastIdx; tileIdx++) {
            BOOL     isVisible = (tileIdx >= visFirst && tileIdx <= visLast);
            TTLTile *tile;

            if (isVisible != (pass == 0)) continue;

            ObtainSemaphore(&inst->listSem);
            tile = ttl_tile_acquire(inst, ttl_tile_base(tileIdx));
            if (tile && tile->dirty) {
                ttl_render_tile(inst, tile);
                if (isVisible) visibleRendered++;
            }
            ReleaseSemaphore(&inst->listSem);
        }
    }

    return visibleRendered;
}

/* ------------------------------------------------------------------ */
/* GM_LAYOUT                                                            */
/* ------------------------------------------------------------------ */

ULONG TTL_OnLayout(Class *cl, Object *o, struct gpLayout *msg)
{
    TTLData      *inst   = TTL_DATA(cl, o);
    // struct Gadget    *g   = G(o);
    // struct GadgetInfo *gi = msg->gpl_GInfo;
    // struct Screen    *sc  = gi && gi->gi_Window ? gi->gi_Window->WScreen : NULL;

    inst->layoutToDo = TRUE;

    return TRUE;
}

/* ------------------------------------------------------------------ */
/* GM_DOMAIN                                                            */
/* ------------------------------------------------------------------ */

ULONG TTL_OnDomain(Class *cl, Object *o, struct gpDomain *msg)
{
    TTLData      *inst   = TTL_DATA(cl, o);
    struct IBox  *domain = &msg->gpd_Domain;
    WORD asz = (inst->style && inst->style->avatarSize > 0)
               ? inst->style->avatarSize : 35;

    switch (msg->gpd_Which) {
        case GDOMAIN_MINIMUM:
            domain->Width  = (WORD)(asz * 4);
            domain->Height = 8; /* for the window layout sake */
            break;
        case GDOMAIN_MAXIMUM:
            domain->Width  = 32767;
            domain->Height = 32767;
            break;
        case GDOMAIN_NOMINAL:
        default:
            domain->Width  = 200;
            domain->Height = (WORD)(asz * 20);
            break;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Waiting screen (see TTIMELINE_ViewMode / ttl_is_waiting)              */
/* ------------------------------------------------------------------ */

#define TTL_WAIT_DEFAULT_TEXT "Loading\xE2\x80\xA6"   /* "Loading…" */
#define TTL_WAIT_GAP 8   /* pixels between image and text */

static void ttl_render_wait(TTLData *inst, struct RastPort *rp, struct Gadget *g)
{
    WORD  gadLeft = g->LeftEdge, gadTop = g->TopEdge;
    WORD  gadW    = g->Width,    gadH   = g->Height;
    BmImage *img  = &inst->style->waitImage;
    BOOL  haveImg = BmImage_IsLoaded(img);
    WORD  imgW    = haveImg ? (WORD)img->width  : 0;
    WORD  imgH    = haveImg ? (WORD)img->height : 0;
    const char *text = (inst->waitText && inst->waitText[0])
                       ? inst->waitText : TTL_WAIT_DEFAULT_TEXT;
    WORD  textW = 0, textH = 0, textAscent = 0;
    WORD  blockH, blockTop;

    SetAPen(rp, (LONG)FS3E_PEN(inst->style, FS3E_COLOR_TIMELINE_BG));
    SetDrMd(rp, JAM1);
    RectFill(rp, gadLeft, gadTop, gadLeft + gadW - 1, gadTop + gadH - 1);

    if (inst->style->dcNormal && text[0]) {
        struct URPTextMetric tm;
        URPDC_TextSizeUTF8(inst->style->dcNormal, text, -1, &tm);
        textW      = (tm.width  > 0) ? (WORD)tm.width  : 0;
        textH      = (tm.height > 0) ? (WORD)tm.height : inst->lineHeight;
        textAscent = (tm.baseY  > 0) ? (WORD)tm.baseY  : inst->lineAscent;
    }

    blockH   = (WORD)(imgH + ((haveImg && textW > 0) ? TTL_WAIT_GAP : 0) + textH);
    blockTop = (WORD)(gadTop + (gadH - blockH) / 2);

    if (haveImg) {
        WORD imgX = (WORD)(gadLeft + (gadW - imgW) / 2);
        BltBitMapRastPort(img->bitmap, 0, 0, rp, imgX, blockTop, imgW, imgH, 0xC0);
    }

    if (textW > 0) {
        struct URPTextPos pos;
        ULONG txtPen = FS3E_PEN(inst->style, FS3E_COLOR_TEXT);
        ULONG bgPen  = FS3E_PEN(inst->style, FS3E_COLOR_TIMELINE_BG);

        pos.x = (WORD)(gadLeft + (gadW - textW) / 2);
        pos.y = (WORD)(blockTop + (haveImg ? imgH + TTL_WAIT_GAP : 0) + textAscent);

        URPDC_SetDrawColorFromPen(inst->style->dcNormal, inst->screen,
                                  (LONG)txtPen, (LONG)bgPen);
        SetAPen(rp, (LONG)txtPen);
        SetBPen(rp, (LONG)bgPen);
        SetDrMd(rp, JAM2);
        URPDrawTextUTF8(rp, inst->style->dcNormal, &pos, text, (ULONG)(-1));
    }
}

/* ------------------------------------------------------------------ */
/* GM_RENDER                                                            */
/* ------------------------------------------------------------------ */

ULONG TTL_OnRender(Class *cl, Object *o, struct gpRender *msg)
{
    TTLData         *inst = TTL_DATA(cl, o);
    struct RastPort *rp   = msg->gpr_RPort;
    struct Gadget   *g    = G(o);

    WORD  gadLeft, gadTop, gadW, gadH;
    LONG  warmTop, warmBot;
    LONG  tileIdx, firstIdx, lastIdx;

    gadLeft = g->LeftEdge;
    gadTop  = g->TopEdge;
    gadW    = g->Width;
    gadH    = g->Height;

    if (gadW <= 0 || gadH <= 0 || msg->gpr_GInfo == NULL ||
        msg->gpr_GInfo->gi_Screen == NULL
        || inst->style == NULL) return TRUE;

    /* note should check if color mode and depth changed */
    inst->screen =  msg->gpr_GInfo->gi_Screen;
    {
        struct Task *t=FindTask(NULL);
//bdbprintf("dr1 inst->callerTask %08x here:%08x\n",(int)inst->callerTask,(int)t);
        if(t != inst->callerTask ||
            rp == NULL)
        {
            /* sorry, but on the right process will you ? */
            ttl_notify(cl, o, msg->gpr_GInfo, TTIMELINE_ProcessRefresh, TRUE);
            return TRUE;
        }
    }

    /* From here on we're touching channel post lists / scroll extents
     * that GM_HANDLEINPUT's hit-testing can also touch from a different
     * task -- see the listSem comment in fs3etoottimeline_private.h.
     * Held across ttl_do_layout too, since a width change relayouts and
     * rebuilds Y positions for every channel, not just the active one. */
    ObtainSemaphore(&inst->listSem);

    /* Normally already done by TTLM_Prepare -- still needed here for the
     * refreshes Intuition/layout.gadget trigger on their own (resize). */
    ttl_layout_and_scroll(cl, o, rp);

    if (ttl_is_waiting(inst)) {
        ttl_render_wait(inst, rp, g);
       /* now use else so we have still the resize grip renderered */
    } else
    {
        /* classic list draw mode start here */

    {
    TTLChannel *active = ttl_active(inst);

    if (inst->tileCount == 0) {
        /* Pool not ready – paint solid background */
        SetAPen(rp, (LONG)FS3E_PEN(inst->style, FS3E_COLOR_TIMELINE_BG));
        RectFill(rp, gadLeft, gadTop, gadLeft+gadW-1, gadTop+gadH-1);
        ReleaseSemaphore(&inst->listSem);
        return 0;
    }

    /* Warm window: a buffer of TTL_TILE_BUF tile rows above and below
     * the visible area ensures tiles are ready when the user scrolls. */
    warmTop = active->scrollY - (LONG)TTL_TILE_BUF * TTL_TILE_HEIGHT;
    warmBot = active->scrollY + gadH + (LONG)TTL_TILE_BUF * TTL_TILE_HEIGHT;

    /* Evict cold tiles */
    ttl_tile_evict_out_of_range(inst, warmTop, warmBot);

    /* Blit only the visible rows -- the warm buffer rows are rendered
     * ahead of time by TTLM_Prepare, outside the layer lock. A row still
     * dirty here (refresh that didn't go through TTLM_Prepare) is
     * rendered inline as a fallback, so the display is never stale. */
    firstIdx = ttl_tile_index(active->scrollY);
    lastIdx  = ttl_tile_index(active->scrollY + gadH - 1);

    for (tileIdx = firstIdx; tileIdx <= lastIdx; tileIdx++) {
        LONG      tileBaseY = ttl_tile_base(tileIdx);
        TTLTile  *tile      = ttl_tile_acquire(inst, tileBaseY);

        if (!tile) continue;

        if (tile->dirty)
            ttl_render_tile(inst, tile);

        /* Only blit the portion that intersects the visible gadget area */
        {
            LONG visTop = active->scrollY;
            LONG visBot = active->scrollY + gadH;

            /* Intersection of tile and visible window in timeline coords */
            LONG clipTop = tileBaseY > visTop ? tileBaseY : visTop;
            LONG clipBot = (tileBaseY + TTL_TILE_HEIGHT) < visBot
                           ? (tileBaseY + TTL_TILE_HEIGHT) : visBot;

            if (clipBot <= clipTop) continue;

            /* Source Y within the tile bitmap */
            WORD srcY = (WORD)(clipTop - tileBaseY);
            /* Destination Y within the gadget */
            WORD dstY = (WORD)(gadTop + (clipTop - visTop));
            WORD rows = (WORD)(clipBot - clipTop);

            BltBitMapRastPort(tile->bm, 0, srcY,
                              rp, gadLeft, dstY,
                              gadW, rows, 0xC0);
        }
    }

    /* Fill the area above the first post if visible -- topContentY is
     * contentTopY (list start), EXCEPT with a profile header (outside
     * the list, see TTLChannel.headerPost), where the header itself
     * already occupies and tile-renders [0, header->height): naively
     * using contentTopY here would paint a flat background rect right
     * over the already-drawn header whenever it's scrolled into view.
     * ttl_channel_min_scroll() already computes exactly this same
     * "topmost real content Y" for the scroll clamp, so it doubles as
     * the right value here too. */
    if (ttl_channel_min_scroll(active) > active->scrollY) {
        WORD fillBot = (WORD)(gadTop + (ttl_channel_min_scroll(active) - active->scrollY));
        if (fillBot > gadTop) {
            SetAPen(rp, (LONG)FS3E_PEN(inst->style, FS3E_COLOR_TIMELINE_BG));
            RectFill(rp, gadLeft, gadTop, gadLeft+gadW-1, fillBot-1);
        }
    }

    /* Fill the area below the last post if visible */
    if (active->contentBottomY < active->scrollY + gadH) {
        WORD fillTop = (WORD)(gadTop + (active->contentBottomY - active->scrollY));
        if (fillTop < gadTop + gadH) {
            if (fillTop < gadTop) fillTop = gadTop;
            SetAPen(rp, (LONG)FS3E_PEN(inst->style, FS3E_COLOR_TIMELINE_BG));
            RectFill(rp, gadLeft, fillTop, gadLeft+gadW-1, gadTop+gadH-1);
        }
    }

    /* Auto-trigger "load older" once the pinned bottom row (TTLLoadOlder_
     * Class, see ttl_channel_add_boundaries) is at or near the bottom of
     * the visible viewport -- mobile-app style, no click needed (unlike
     * the "look for something new" row at the top, which does have a
     * hot-spot -- see the TTLItemClass comment). Fires the exact same
     * generic notify a click would; olderLoadTriggered latches it so this
     * only fires once per arrival at the bottom (reset when new older
     * content actually lands, see ttl_channel_insert_bottom). postCount>0
     * guards mlh_TailPred, which is only a real node once the channel has
     * at least one real post (see the boundary-insertion comment). */
    if (active->postCount > 0 && !active->olderLoadTriggered) {
        TTLPost *tail = (TTLPost *)active->posts.mlh_TailPred;
        if (tail->cls == &TTLLoadOlder_Class &&
            active->scrollY + gadH >= active->contentBottomY)
        {
            active->olderLoadTriggered = TRUE;
            ttl_notify_hotspot(cl, o, msg->gpr_GInfo, TTL_HOT_LOAD_OLDER, NULL, 0, NULL, FALSE, FALSE, FALSE, FALSE, FALSE, NULL, NULL, NULL, NULL, NULL);
        }
    }

    } /* end active-channel scope */

    } /* end else of wait mode, fallback to grip display  */

    ReleaseSemaphore(&inst->listSem);

    /* TODO: text-selection COMPLEMENT overlay (future: walk selSpanA..selSpanB) */

    /* Resize grip: three short diagonal lines in the bottom-right corner,
     * drawn directly onto the window RastPort so they survive tile blitting. */
    {
        WORD cx = (WORD)(gadLeft + gadW);   /* right edge (exclusive) */
        WORD cy = (WORD)(gadTop  + gadH);   /* bottom edge (exclusive) */
        int  i;
        SetAPen(rp, (LONG)FS3E_PEN(inst->style, FS3E_COLOR_ACCENT));
        for (i = 3; i <= TTL_RESIZE_HANDLE - 1; i += 4) {
            Move(rp, cx - 1 - i, cy - 1);
            Draw(rp, cx - 1,     cy - 1 - i);
        }
    }

    return 0;
}
