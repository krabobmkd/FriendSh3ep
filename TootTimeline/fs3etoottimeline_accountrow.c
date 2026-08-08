/*
 * TootTimeline — account-only row (TTLAccountRow_Class).
 *
 * Shown for account search results and a user's followers/following lists
 * (see TTLPostSetup.isAccountRow) -- a normal scrolling list item, added via
 * TTIMELINE_AddPost/AppendPost exactly like a toot or TTLNotifFollow_Class,
 * whose layout this borrows most of. Deliberately thinner than every other
 * row kind: just display name (big font) + @user@instance (small grey
 * font), NO avatar at all -- these lists can be long (account search,
 * follower/following counts routinely run into the hundreds), and skipping
 * the avatar means zero FETCH_IMAGE/thumbnail traffic per row.
 */

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/alib.h>
#include <string.h>

#include "fs3etoottimeline_private.h"

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

/* ------------------------------------------------------------------ */
/* ttl_account_row_alloc                                                */
/* ------------------------------------------------------------------ */

/* Takes the same TTLPostSetup a toot does -- only username/acct/postId are
 * read (see TTLPostSetup.isAccountRow's doc comment). */
TTLPost *ttl_account_row_alloc(const TTLPostSetup *setup)
{
    TTLPost *post = (TTLPost *)AllocVec(sizeof(TTLPost), MEMF_ANY | MEMF_CLEAR);
    if (!post) return NULL;

    NewList((struct List *)&post->textSpans);
    post->cls           = &TTLAccountRow_Class;
    post->dirty         = TRUE;
    post->hotSpotBucket = -1;
    post->hotSpotsDirty = TRUE;

    if (setup) {
        post->username = dup_str(setup->username);
        post->acct     = dup_str(setup->acct);
        post->postId   = dup_str(setup->postId);
    }

    return post;
}

static void ttl_account_row_dispose(TTLPost *post)
{
    if (post->username) FreeVec(post->username);
    if (post->acct)     FreeVec(post->acct);
    if (post->postId)   FreeVec(post->postId);
}

/* ------------------------------------------------------------------ */
/* ttl_account_row_layout                                               */
/* ------------------------------------------------------------------ */

static void ttl_account_row_layout(TTLData *inst, TTLPost *post)
{
    WORD padLeft, textX;
    LONG curRelY;

    ttl_clear_textspans(post);

    padLeft = (inst->style && inst->style->avatarSize > 0) ? inst->style->postPadLeft : 6;
    /* No avatar column at all -- textX is just the left padding, unlike
     * every other row kind (which reserves avatarW + avatarGap first). */
    textX = padLeft;

    curRelY = TTL_POST_PAD_TOP;

    /* Display name */
    if (post->username && post->username[0]) {
        TTLTextSpan *sp = ttl_span_alloc(post->username, TTL_SPAN_USERNAME, curRelY, textX, inst);
        if (sp) AddTail((struct List *)&post->textSpans, (struct Node *)&sp->node);
    }

    /* "Follows you" badge -- inline on the same row as the display name,
     * right-aligned within the row's available width (see followedBy's
     * comment in fs3etoottimeline_private.h for why only followedBy, not
     * following, drives this). Reuses TTL_SPAN_TIMESTAMP purely to
     * inherit its existing dim/mini styling from ttl_dc_for_span/
     * .render's switch below -- this row has no actual timestamp, the
     * span type is just borrowed for its look. Width comes from the span
     * ttl_span_alloc already measured, not re-derived (same "don't
     * duplicate a measurement" rule as everywhere else in this file's
     * sibling profile-header code) -- x is computed only after alloc,
     * once that width is known, then patched onto the span directly.
     * postRelY is nudged so this smaller font's baseline lines up with
     * the display name's own baseline instead of both sharing the same
     * span top (which would look like the badge floats above the name). */
    if (post->followedBy && inst->style && inst->style->dcMini) {
        WORD rowW = (WORD)(inst->gadWidth - textX - TTL_POST_PAD_RIGHT);
        LONG badgeRelY = curRelY + inst->nameLineAscent - inst->miniLineAscent;
        TTLTextSpan *sp = ttl_span_alloc("Follows you", TTL_SPAN_TIMESTAMP,
                                          badgeRelY, textX, inst);
        if (sp) {
            WORD badgeX = (WORD)(textX + rowW - sp->width);
            if (badgeX < textX) badgeX = textX;
            sp->x = badgeX;
            AddTail((struct List *)&post->textSpans, (struct Node *)&sp->node);
        }
    }

    curRelY += inst->nameLineHeight;

    /* @acct */
    if (post->acct && post->acct[0]) {
        TTLTextSpan *sp = ttl_span_alloc(post->acct, TTL_SPAN_ACCT, curRelY, textX, inst);
        if (sp) AddTail((struct List *)&post->textSpans, (struct Node *)&sp->node);
    }
    curRelY += inst->miniLineHeight;

    /* No avatar-height floor here (deliberately, unlike every other row
     * kind) -- this row is exactly as tall as its two text lines need,
     * nothing more. */
    curRelY += TTL_POST_PAD_BOT;
    curRelY += 1; /* separator pixel, drawn generically by the tile-render caller */
    post->height = curRelY;
    post->dirty  = TRUE;
    post->hotSpotsDirty = TRUE;
}

/* ------------------------------------------------------------------ */
/* ttl_account_row_render                                              */
/* ------------------------------------------------------------------ */

static void ttl_account_row_render(TTLData *inst, struct RastPort *rp,
                                     TTLPost *post, LONG tileBaseY)
{
    LONG bgpen = (LONG)FS3E_PEN(inst->style, FS3E_COLOR_TIMELINE_BG);
    WORD drawY = (WORD)(post->timelineY - tileBaseY);
    TTLTextSpan *sp;

    for (sp = (TTLTextSpan *)post->textSpans.mlh_Head;
         sp->node.mln_Succ;
         sp = (TTLTextSpan *)sp->node.mln_Succ)
    {
        struct URPDrawContext *dc;
        LONG pen;
        struct URPTextPos pos;

        if (!sp->utf8 || !sp->utf8[0]) continue;

        switch (sp->spanType) {
            case TTL_SPAN_USERNAME:
                dc  = inst->style->dcUsername;
                pen = (LONG)FS3E_PEN(inst->style, FS3E_COLOR_USERNAME);
                break;
            default: /* TTL_SPAN_ACCT */
                dc  = inst->style->dcMini;
                pen = (LONG)FS3E_PEN(inst->style, FS3E_COLOR_TEXT_DIM);
                break;
        }
        if (!dc) continue;

        URPDC_SetDrawColorFromPen(dc, inst->screen, pen, bgpen);
        pos.x = sp->x;
        pos.y = (WORD)(drawY + sp->postRelY + sp->ascent);
        URPDrawTextUTF8(rp, dc, &pos, sp->utf8, (ULONG)sp->charCount);
    }
}

/* ------------------------------------------------------------------ */
/* ttl_account_row_build_hotspots                                       */
/* ------------------------------------------------------------------ */

/* One hotspot covering the whole row -- reuses TTL_HOT_AVATAR verbatim
 * (data = post->acct), same "click -> open this account's profile"
 * behavior every other avatar/handle hotspot already has. No new hotspot
 * type or friendsh3ep.c wiring needed. */
static void ttl_account_row_build_hotspots(TTLData *inst, TTLPost *post)
{
    post->hotSpotCount = 0;

    if (post->acct && post->acct[0])
        ttl_hs_add(post, TTL_HOT_AVATAR, 0, 0,
                   inst->gadWidth, (WORD)post->height,
                   post->acct, (ULONG)strlen(post->acct));
}

/* ------------------------------------------------------------------ */

const TTLItemClass TTLAccountRow_Class = {
    ttl_account_row_layout,
    ttl_account_row_render,
    ttl_account_row_build_hotspots,
    NULL, /* .activate: the generic ttl_notify_hotspot() already fires for
           * TTL_HOT_AVATAR -- nothing kind-specific to do locally. */
    ttl_account_row_dispose
};
