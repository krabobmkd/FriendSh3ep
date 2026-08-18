/*
 * TootTimeline — server/instance info header row (TTLInstanceHeader_Class).
 *
 * Shown pinned above a channel's post list when a "tell me about this
 * server" lookup is open (see TTIMELINE_ShowInstanceInfo in
 * fs3etoottimeline.h) -- the search-driven counterpart to
 * TTIMELINE_ShowProfile's user profile header (fs3etoottimeline_profile.c),
 * reusing the exact same TTLChannel.headerPost slot/mechanism. Deliberately
 * much simpler than the profile header: no avatar, no Follow/Message
 * buttons, no Followers/Following boxes -- just a title line, an optional
 * subtitle line, and one big word-wrapped text block the caller
 * (FS3EApp_SearchInstance, fs3erequests.c) has already formatted with every
 * field worth showing (description, char/media/poll limits, translation
 * support, registrations, contact, rules, ...). TootTimeline itself stays
 * ignorant of what a Mastodon instance even is -- it just renders a title +
 * subtitle + wrapped body, same "dumb renderer, app owns the policy"
 * split TTIMELINE_ShowProfile already follows for a bio.
 */

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/alib.h>
#include <string.h>

#include "fs3etoottimeline_private.h"
#include "../fs3etextwrap.h"

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
/* ttl_instance_header_alloc                                            */
/* ------------------------------------------------------------------ */

TTLPost *ttl_instance_header_alloc(const TTLInstanceHeaderSetup *setup)
{
    TTLPost *post = (TTLPost *)AllocVec(sizeof(TTLPost), MEMF_ANY | MEMF_CLEAR);
    if (!post) return NULL;

    NewList((struct List *)&post->textSpans);
    post->cls           = &TTLInstanceHeader_Class;
    post->dirty         = TRUE;
    post->hotSpotBucket = -1;
    post->hotSpotsDirty = TRUE;

    if (setup) {
        /* Reuses TTLPost's generic username/acct/body string slots for this
         * row kind's own concepts (domain/subtitle/info-block) -- same
         * "borrow the existing slots rather than add row-kind-specific
         * fields" convention TTLProfileHeader_Class already uses (postId
         * doubling as account id there). */
        post->username = dup_str(setup->domain);
        post->acct     = (setup->subtitle && setup->subtitle[0]) ? dup_str(setup->subtitle) : NULL;
        post->body     = dup_str(setup->body);
    }

    return post;
}

static void ttl_instance_header_dispose(TTLPost *post)
{
    if (post->username) FreeVec(post->username);
    if (post->acct)      FreeVec(post->acct);
    if (post->body)       FreeVec(post->body);
}

/* ------------------------------------------------------------------ */
/* ttl_instance_header_layout                                           */
/* ------------------------------------------------------------------ */

static void ttl_instance_header_layout(TTLData *inst, TTLPost *post)
{
    WORD padLeft, textX, textW;
    LONG curRelY;

    ttl_clear_textspans(post);

    padLeft = (inst->style) ? inst->style->postPadLeft : 6;
    textX   = padLeft;
    textW   = (WORD)(inst->gadWidth - textX - TTL_POST_PAD_RIGHT);
    if (textW < 32) textW = 32;

    curRelY = TTL_POST_PAD_TOP;

    /* Domain/title -- always reserves a line, even if somehow empty, so
     * .render/.buildHotspots never have to special-case a shifted body
     * start (same "advance unconditionally" convention
     * ttl_profile_header_layout uses for the display-name line). */
    if (post->username && post->username[0]) {
        TTLTextSpan *sp = ttl_span_alloc(post->username, TTL_SPAN_USERNAME, curRelY, textX, inst);
        if (sp) AddTail((struct List *)&post->textSpans, (struct Node *)&sp->node);
    }
    curRelY += inst->nameLineHeight;

    /* Subtitle (e.g. "Mastodon 4.2.1") -- optional, only reserves a line
     * when actually present (unlike the domain line above): a server that
     * didn't answer with a version string shouldn't leave a blank gap. */
    if (post->acct && post->acct[0]) {
        TTLTextSpan *sp = ttl_span_alloc(post->acct, TTL_SPAN_ACCT, curRelY, textX, inst);
        if (sp) AddTail((struct List *)&post->textSpans, (struct Node *)&sp->node);
        curRelY += inst->miniLineHeight;
    }

    curRelY += 4; /* gap above the info block */

    /* Info block, word-wrapped exactly like a toot body/bio -- one
     * TTL_SPAN_BODY span per visual row. The caller has already folded
     * every field worth showing (description, limits, translation support,
     * registrations, contact, rules, ...) into this one plain-text blob,
     * blank lines and all -- TootTimeline doesn't know or care what any of
     * it means, same as it doesn't parse a toot's own body. */
    if (post->body && post->body[0]) {
        struct URPDrawContext *dcBody = inst->style ? inst->style->dcNormal : NULL;
        if (dcBody) {
            FS3ETextWrap tw;
            if (FS3ETextWrap_Build(&tw, dcBody, inst->lineHeight, post->body, textW)) {
                ULONG i;
                for (i = 0; i < tw.rowCount; i++) {
                    TTLTextSpan *sp = ttl_span_from_row(&tw.rows[i], textX, curRelY, inst);
                    if (sp) AddTail((struct List *)&post->textSpans, (struct Node *)&sp->node);
                    curRelY += inst->lineHeight;
                }
                FS3ETextWrap_Free(&tw);
            }
        } else {
            curRelY += ttl_count_wrapped_lines(inst, post->body, textW) * inst->lineHeight;
        }
    }

    curRelY += TTL_POST_PAD_BOT;
    curRelY += 1; /* separator pixel, drawn generically by the tile-render caller */
    post->height = curRelY;
    post->dirty  = TRUE;
    post->hotSpotsDirty = TRUE;
}

/* ------------------------------------------------------------------ */
/* ttl_instance_header_render                                           */
/* ------------------------------------------------------------------ */

static void ttl_instance_header_render(TTLData *inst, struct RastPort *rp,
                                        TTLPost *post, LONG tileBaseY)
{
    LONG  bgpen = (LONG)FS3E_PEN(inst->style, FS3E_COLOR_PROFILE_HEADER_BG);
    WORD  drawY = (WORD)(post->timelineY - tileBaseY);
    TTLTextSpan *sp;

    /* Same distinct-background-block treatment as the profile header --
     * reuses FS3E_COLOR_PROFILE_HEADER_BG so both pinned-header kinds read
     * consistently as "not a toot" at a glance. */
    {
        WORD fillTop = drawY > 0 ? drawY : 0;
        WORD fillBot = (WORD)(drawY + post->height);
        if (fillBot > TTL_TILE_HEIGHT) fillBot = TTL_TILE_HEIGHT;
        if (fillBot > fillTop) {
            SetAPen(rp, bgpen);
            RectFill(rp, 0, fillTop, (WORD)(inst->gadWidth - 1), (WORD)(fillBot - 1));
        }
    }

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
            case TTL_SPAN_ACCT:
                dc  = inst->style->dcMini;
                pen = (LONG)FS3E_PEN(inst->style, FS3E_COLOR_TEXT_DIM);
                break;
            default: /* TTL_SPAN_BODY -- the info block */
                dc  = inst->style->dcNormal;
                pen = (LONG)FS3E_PEN(inst->style, FS3E_COLOR_TEXT);
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
/* ttl_instance_header_build_hotspots                                   */
/* ------------------------------------------------------------------ */

static void ttl_instance_header_build_hotspots(TTLData *inst, TTLPost *post)
{
    TTLTextSpan *sp;

    (void)inst;
    post->hotSpotCount = 0;

    /* Reuses the same @mention/#hashtag/URL scanner a toot body already
     * gets -- a description mentioning a contact URL, or an account acct
     * string embedded in the info block, becomes clickable for free, same
     * as ttl_profile_header_build_hotspots does for a bio. */
    for (sp = (TTLTextSpan *)post->textSpans.mlh_Head;
         sp->node.mln_Succ && post->hotSpotCount < TTL_HOTSPOT_MAX_PER_TOOT;
         sp = (TTLTextSpan *)sp->node.mln_Succ)
    {
        if (sp->spanType == TTL_SPAN_BODY)
            ttl_scan_span_tokens(post, sp);
    }
}

/* ------------------------------------------------------------------ */

const TTLItemClass TTLInstanceHeader_Class = {
    ttl_instance_header_layout,
    ttl_instance_header_render,
    ttl_instance_header_build_hotspots,
    NULL, /* .activate: hot-spots here are only ever URL/hashtag/mention
           * tokens inside the info block -- the generic ttl_notify_hotspot()
           * already fires for those, nothing kind-specific to do locally. */
    ttl_instance_header_dispose
};
