/*
 * TootTimeline — trending-link "news" card (TTLNewsCard_Class).
 *
 * VIEWMODE_News's own row kind (see TTLPostSetup.isNewsCard) -- Mastodon's
 * "Explore/News" trending-links list (GET /api/v1/trends/links). A normal
 * scrolling list item, added via TTIMELINE_AddPost/AppendPost exactly like
 * a toot or TTLAccountRow_Class, but rendered as one big full-width card
 * instead of a small side rectangle: unlike a toot's embedded link-preview
 * card (see TTLToot_Class's own cardX/Y/W/H in fs3etoottimeline_posts.c/
 * _tiles.c, which this deliberately mirrors the *fields* of), there's no
 * toot body competing for width here, so the full gadget width goes to
 * text -- more words on screen at once, which is the whole point on
 * Amiga's own browsers struggling with modern news sites.
 *
 * Reuses TTLPost's existing card fields (cardUrl/cardTitle/cardDescription/
 * cardProviderName/cardImageUrl/cardTitleLines/cardDescLines/cardImgH/
 * cardX/Y/W/H) rather than adding new ones -- this row's ENTIRE content IS
 * a card, so borrowing the same slots TTLToot_Class already populates for
 * its own embedded card costs nothing and needs no new TTLPost fields.
 * `timestamp` (otherwise toot-only) doubles as the link's published date.
 *
 * Whole card is a single TTL_HOT_CARD hot-spot (data = the article URL) --
 * reuses friendsh3ep.c's existing handler verbatim (already opens the URL
 * via ManageUrl for a toot's own embedded card), no new hot-spot type or
 * app-side wiring needed.
 */

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/alib.h>
#include <string.h>

#include "fs3etoottimeline_private.h"
#include "../fs3etextwrap.h"
#include "../avatarimages.h"
#include "../bmimage.h"

/* Hero image height at the default 12pt font size -- bigger than
 * TTL_CARD_IMAGE_BASE_H's 90 (a toot's own small embedded card), since
 * this card owns the whole row width and can afford a real header image.
 * Scaled by the same avatarSize/TTL_AVATAR_BASE_SIZE ratio every other
 * DPI-scaled dimension in this codebase uses. */
#define TTL_NEWSCARD_IMG_BASE_H 160

/* Article title/description wrap caps -- together these are the "3 or 4
 * lines" of actual article text the card shows (title first, usually
 * shorter; description fills the rest), on top of the separate provider-
 * name/date lines above them. */
#define TTL_NEWSCARD_TITLE_MAX_ROWS 2
#define TTL_NEWSCARD_DESC_MAX_ROWS  2

static char *dup_str(const char *s)
{
    ULONG len;
    char *copy;
    if (!s) return NULL;
    len  = (ULONG)strlen(s);
    copy = (char *)AllocVec(len + 1, MEMF_ANY);
    if (copy) CopyMem((APTR)s, copy, len + 1);
    return copy;
}

static char *dup_strn(const char *s, ULONG len)
{
    char *copy = (char *)AllocVec(len + 1, MEMF_ANY);
    if (copy) {
        CopyMem((APTR)s, copy, len);
        copy[len] = '\0';
    }
    return copy;
}

/* "YYYY-MM-DDTHH:MM:SS..." -> "YYYY-MM-DD" -- just the date part, locale-
 * independent (no month-name table needed), same "good enough, not fancy"
 * choice ttl_format_timestamp_lines makes for a toot's own corner
 * timestamp elsewhere in this codebase. dst must be >= 11 bytes. */
static void ttl_news_date_only(const char *iso, char *dst, ULONG dstSize)
{
    ULONG n = 0;
    dst[0] = '\0';
    if (!iso) return;
    while (iso[n] && iso[n] != 'T' && n < dstSize - 1) n++;
    CopyMem((APTR)iso, dst, n);
    dst[n] = '\0';
}

/* Simple UTF-8-safe text draw, same "URPDrawTextUTF8 with an auto-computed
 * length" convention fs3etoottimeline_tiles.c's own (private) tile_draw_text
 * uses -- not accessible from here, so reimplemented locally. */
static void ttl_news_draw_text(struct RastPort *rp, WORD x, WORD y,
                                const char *utf8, struct URPDrawContext *dc)
{
    struct URPTextPos pos;
    if (!dc || !utf8 || !utf8[0]) return;
    pos.x = x;
    pos.y = y;
    URPDrawTextUTF8(rp, dc, &pos, utf8, -1);
}

/* ------------------------------------------------------------------ */
/* ttl_news_card_alloc                                                  */
/* ------------------------------------------------------------------ */

TTLPost *ttl_news_card_alloc(const TTLPostSetup *setup)
{
    TTLPost *post = (TTLPost *)AllocVec(sizeof(TTLPost), MEMF_ANY | MEMF_CLEAR);
    if (!post) return NULL;

    NewList((struct List *)&post->textSpans);
    post->cls           = &TTLNewsCard_Class;
    post->dirty         = TRUE;
    post->hotSpotBucket = -1;
    post->hotSpotsDirty = TRUE;

    if (setup) {
        post->hasCard          = TRUE;
        post->cardUrl          = dup_str(setup->cardUrl);
        post->cardTitle        = dup_str(setup->cardTitle);
        post->cardDescription  = dup_str(setup->cardDescription);
        post->cardProviderName = dup_str(setup->cardProviderName);
        post->cardImageUrl     = (setup->cardImageUrl && setup->cardImageUrl[0])
                                ? dup_str(setup->cardImageUrl) : NULL;
        post->timestamp        = dup_str(setup->timestamp); /* published date, see file header */
    }

    return post;
}

static void ttl_news_card_dispose(TTLPost *post)
{
    ULONG i;
    if (post->cardUrl)          FreeVec(post->cardUrl);
    if (post->cardTitle)        FreeVec(post->cardTitle);
    if (post->cardDescription)  FreeVec(post->cardDescription);
    if (post->cardProviderName) FreeVec(post->cardProviderName);
    if (post->cardImageUrl)     FreeVec(post->cardImageUrl);
    if (post->timestamp)        FreeVec(post->timestamp);
    for (i = 0; i < post->cardTitleLineCount; i++)
        if (post->cardTitleLines[i]) FreeVec(post->cardTitleLines[i]);
    for (i = 0; i < post->cardDescLineCount; i++)
        if (post->cardDescLines[i]) FreeVec(post->cardDescLines[i]);
}

/* ------------------------------------------------------------------ */
/* ttl_news_card_layout                                                 */
/* ------------------------------------------------------------------ */

static void ttl_news_card_layout(TTLData *inst, TTLPost *post)
{
    WORD padLeft, cardW, cardTextW, cardPad = 6;
    WORD avatarW;
    LONG curRelY;
    struct URPDrawContext *dcMini   = inst->style ? inst->style->dcMini   : NULL;
    struct URPDrawContext *dcNormal = inst->style ? inst->style->dcNormal : NULL;
    ULONG i;

    ttl_clear_textspans(post); /* none used by this row kind, but keep the invariant */

    padLeft = (inst->style) ? inst->style->postPadLeft : 6;
    avatarW = (inst->style && inst->style->avatarSize > 0) ? inst->style->avatarSize : 35;

    post->cardX = padLeft;
    cardW = (WORD)(inst->gadWidth - padLeft - TTL_POST_PAD_RIGHT);
    if (cardW < 32) cardW = 32;
    post->cardW = cardW;
    cardTextW = (WORD)(cardW - 2 * cardPad);
    if (cardTextW < 16) cardTextW = 16;

    for (i = 0; i < post->cardTitleLineCount; i++)
        if (post->cardTitleLines[i]) { FreeVec(post->cardTitleLines[i]); post->cardTitleLines[i] = NULL; }
    post->cardTitleLineCount = 0;
    for (i = 0; i < post->cardDescLineCount; i++)
        if (post->cardDescLines[i]) { FreeVec(post->cardDescLines[i]); post->cardDescLines[i] = NULL; }
    post->cardDescLineCount = 0;

    curRelY = TTL_POST_PAD_TOP;

    /* Hero image strip, full card width -- see TTL_NEWSCARD_IMG_BASE_H. */
    post->cardImgH = 0;
    if (post->cardImageUrl && post->cardImageUrl[0])
        post->cardImgH = (WORD)(((LONG)TTL_NEWSCARD_IMG_BASE_H * avatarW) / TTL_AVATAR_BASE_SIZE);
    post->cardY = (WORD)curRelY;
    curRelY += post->cardImgH;
    if (post->cardImgH > 0) curRelY += cardPad;

    /* Provider name -- "the name of the news editor", the one line meant
     * to read as a title/heading (bigger font than everything below it,
     * unlike the toot-card's own provider line which is dcMini throughout). */
    if (dcNormal && post->cardProviderName && post->cardProviderName[0])
        curRelY += inst->lineHeight;

    /* Published date, own line right below the provider name. */
    if (dcMini && post->timestamp && post->timestamp[0])
        curRelY += inst->miniLineHeight;

    if (post->cardProviderName || post->timestamp) curRelY += 2; /* small gap before article text */

    /* Article title, wrapped -- see TTL_NEWSCARD_TITLE_MAX_ROWS. */
    if (dcNormal && post->cardTitle && post->cardTitle[0]) {
        FS3ETextWrap tw;
        if (FS3ETextWrap_Build(&tw, dcNormal, inst->lineHeight, post->cardTitle, cardTextW)) {
            ULONG ri, rows = tw.rowCount;
            if (rows > TTL_NEWSCARD_TITLE_MAX_ROWS) rows = TTL_NEWSCARD_TITLE_MAX_ROWS;
            for (ri = 0; ri < rows; ri++) {
                post->cardTitleLines[ri] = dup_strn(tw.rows[ri].start, tw.rows[ri].byteLen);
                post->cardTitleLineCount++;
                curRelY += inst->lineHeight;
            }
            FS3ETextWrap_Free(&tw);
        }
    }

    /* Article description, wrapped -- see TTL_NEWSCARD_DESC_MAX_ROWS.
     * Together with the title above, this is the card's "3 or 4 lines" of
     * actual article text. */
    if (dcMini && post->cardDescription && post->cardDescription[0]) {
        FS3ETextWrap tw;
        if (FS3ETextWrap_Build(&tw, dcMini, inst->miniLineHeight, post->cardDescription, cardTextW)) {
            ULONG ri, rows = tw.rowCount;
            if (rows > TTL_NEWSCARD_DESC_MAX_ROWS) rows = TTL_NEWSCARD_DESC_MAX_ROWS;
            for (ri = 0; ri < rows; ri++) {
                post->cardDescLines[ri] = dup_strn(tw.rows[ri].start, tw.rows[ri].byteLen);
                post->cardDescLineCount++;
                curRelY += inst->miniLineHeight;
            }
            FS3ETextWrap_Free(&tw);
        }
    }

    curRelY += cardPad;
    post->cardH = (WORD)(curRelY - post->cardY);

    curRelY += TTL_POST_PAD_BOT;
    curRelY += 1; /* separator pixel, drawn generically by the tile-render caller */
    post->height = curRelY;
    post->dirty  = TRUE;
    post->hotSpotsDirty = TRUE;
}

/* ------------------------------------------------------------------ */
/* ttl_news_card_render                                                 */
/* ------------------------------------------------------------------ */

static void ttl_news_card_render(TTLData *inst, struct RastPort *rp,
                                  TTLPost *post, LONG tileBaseY)
{
    WORD  drawY = (WORD)(post->timelineY - tileBaseY);
    WORD  rx = post->cardX;
    WORD  ry = (WORD)(drawY + post->cardY);
    WORD  cardRight, cardBottom, rowY;
    WORD  cardPad = 6;
    LONG  bgPen  = (LONG)FS3E_PEN(inst->style, FS3E_COLOR_CARD_BG);
    LONG  txtPen = (LONG)FS3E_PEN(inst->style, FS3E_COLOR_TEXT);
    LONG  dimPen = (LONG)FS3E_PEN(inst->style, FS3E_COLOR_TEXT_DIM);
    struct URPDrawContext *dcMini   = inst->style ? inst->style->dcMini   : NULL;
    struct URPDrawContext *dcNormal = inst->style ? inst->style->dcNormal : NULL;

    if (post->cardW <= 0 || post->cardH <= 0) return;

    cardRight  = (WORD)(rx + post->cardW - 1);
    cardBottom = (WORD)(ry + post->cardH - 1);

    SetAPen(rp, (LONG)bgPen);
    RectFill(rp, rx, ry, cardRight, cardBottom);

    if (post->cardImgH > 0) {
        RgbImage *cimg = (inst->avatarImages && post->cardImageUrl)
                        ? AvatarImages_GetCard(inst->avatarImages, post->cardImageUrl) : NULL;

        if (cimg && RgbImage_IsLoaded(cimg)) {
            ULONG dw, dh;
            WORD  bx, by;
            WORD  boxW = post->cardW, boxH = post->cardImgH;

            if (cimg->width == 0 || cimg->height == 0) {
                SetAPen(rp, (LONG)FS3E_PEN(inst->style, FS3E_COLOR_ACCENT));
                RectFill(rp, rx, ry, (WORD)(rx + post->cardW - 1), (WORD)(ry + post->cardImgH - 1));
                goto news_render_after_image;
            }

            if ((ULONG)boxW * cimg->height <= (ULONG)boxH * cimg->width) {
                dw = boxW;
                dh = ((ULONG)cimg->height * (ULONG)boxW) / cimg->width;
            } else {
                dh = boxH;
                dw = ((ULONG)cimg->width * (ULONG)boxH) / cimg->height;
            }
            if (dw < 1) dw = 1;
            if (dh < 1) dh = 1;

            bx = (WORD)(rx + (boxW - (WORD)dw) / 2);
            by = (WORD)(ry + (boxH - (WORD)dh) / 2);

            RgbImage_DrawScaled(cimg, rp, inst->screen, dcNormal,
                                 bx, by, (UWORD)dw, (UWORD)dh);
        } else {
            SetAPen(rp, (LONG)FS3E_PEN(inst->style, FS3E_COLOR_ACCENT));
            RectFill(rp, rx, ry, (WORD)(rx + post->cardW - 1), (WORD)(ry + post->cardImgH - 1));
        }
news_render_after_image:
        rowY = (WORD)(ry + post->cardImgH + cardPad);
    } else {
        rowY = (WORD)(ry + cardPad);
    }

    if (dcNormal && post->cardProviderName && post->cardProviderName[0]) {
        URPDC_SetDrawColorFromPen(dcNormal, inst->screen,
            (LONG)FS3E_PEN(inst->style, FS3E_COLOR_USERNAME), bgPen);
        ttl_news_draw_text(rp, (WORD)(rx + cardPad), (WORD)(rowY + inst->lineAscent),
                            post->cardProviderName, dcNormal);
        rowY += inst->lineHeight;
    }

    if (dcMini && post->timestamp && post->timestamp[0]) {
        char dateOnly[16];
        ttl_news_date_only(post->timestamp, dateOnly, sizeof(dateOnly));
        URPDC_SetDrawColorFromPen(dcMini, inst->screen, dimPen, bgPen);
        ttl_news_draw_text(rp, (WORD)(rx + cardPad), (WORD)(rowY + inst->miniLineAscent),
                            dateOnly, dcMini);
        rowY += inst->miniLineHeight;
    }

    if (post->cardProviderName || post->timestamp) rowY += 2;

    if (dcNormal) {
        ULONG li;
        URPDC_SetDrawColorFromPen(dcNormal, inst->screen, txtPen, bgPen);
        for (li = 0; li < post->cardTitleLineCount; li++) {
            ttl_news_draw_text(rp, (WORD)(rx + cardPad), (WORD)(rowY + inst->lineAscent),
                                post->cardTitleLines[li], dcNormal);
            rowY += inst->lineHeight;
        }
    }

    if (dcMini) {
        ULONG li;
        URPDC_SetDrawColorFromPen(dcMini, inst->screen, dimPen, bgPen);
        for (li = 0; li < post->cardDescLineCount; li++) {
            ttl_news_draw_text(rp, (WORD)(rx + cardPad), (WORD)(rowY + inst->miniLineAscent),
                                post->cardDescLines[li], dcMini);
            rowY += inst->miniLineHeight;
        }
    }

    /* Thin border, drawn last, same as a toot's own embedded card. */
    SetAPen(rp, (LONG)FS3E_PEN(inst->style, FS3E_COLOR_CARD_BORDER));
    Move(rp, rx, ry);
    Draw(rp, cardRight, ry);
    Draw(rp, cardRight, cardBottom);
    Draw(rp, rx, cardBottom);
    Draw(rp, rx, ry);
}

/* ------------------------------------------------------------------ */
/* ttl_news_card_build_hotspots                                         */
/* ------------------------------------------------------------------ */

/* One hotspot covering the whole card -- reuses TTL_HOT_CARD verbatim
 * (data = the article URL), same click-to-open-URL behavior a toot's own
 * embedded card already has (see TTL_HOT_CARD in friendsh3ep.c). No new
 * hot-spot type or app-side wiring needed. */
static void ttl_news_card_build_hotspots(TTLData *inst, TTLPost *post)
{
    (void)inst;
    post->hotSpotCount = 0;

    if (post->cardUrl && post->cardUrl[0])
        ttl_hs_add(post, TTL_HOT_CARD, 0, 0,
                   inst->gadWidth, (WORD)post->height,
                   post->cardUrl, (ULONG)strlen(post->cardUrl));
}

/* ------------------------------------------------------------------ */

const TTLItemClass TTLNewsCard_Class = {
    ttl_news_card_layout,
    ttl_news_card_render,
    ttl_news_card_build_hotspots,
    NULL, /* .activate: the generic ttl_notify_hotspot() already fires for
           * TTL_HOT_CARD -- nothing kind-specific to do locally. */
    ttl_news_card_dispose
};
