/*
 * SearchBarLayout - BOOPSI layout.gadget subclass wrapping Part C of the
 * FriendSh3ep main window (search editor + TootTimeline).
 *
 * Overrides OM_NEW, OM_SET/OM_UPDATE, GM_DOMAIN, GM_LAYOUT, GM_RENDER.
 * Does NOT call super in GM_LAYOUT or GM_RENDER -- positions/renders all
 * children directly, same reasoning as TitleBarLayout (see
 * fs3etitlebar.c's file header): layout.gadget's own GM_RENDER doesn't
 * know about bounds our own GM_LAYOUT computed, so it can't be trusted to
 * skip the hidden search editor. All other methods chain to layout.gadget
 * via DoSuperMethodA.
 *
 * See fs3esearchbar.h for the show/hide design.
 */

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include <intuition/gadgetclass.h>
#include <gadgets/layout.h>
#include <proto/layout.h>

#include "../compilers.h"
#include "fs3esearchbar.h"

#ifndef G
#define G(o) ((struct Gadget *)(o))
#endif

/* chooser.gadget's own GM_DOMAIN(MINIMUM) query (via getChildDomain's
 * NULL-RPort probe) reports a width too thin to show its label text --
 * same reason fs3etootview.c's visibilityChooser needs an explicit
 * CHILD_MinWidth 100 tag when added to a real layout.gadget. This class
 * bypasses that weighting mechanism (children are positioned by hand in
 * GM_LAYOUT), so the same floor is enforced directly here instead. */
#define SBLAYOUT_CHOOSER_MINW 100

/* Same reasoning as SBLAYOUT_CHOOSER_MINW above, for the back button --
 * its own GM_DOMAIN(MINIMUM) query through the NULL-RPort probe isn't
 * trustworthy either, so a floor is enforced by hand here too. */
#define SBLAYOUT_BACK_MINW 60

typedef struct {
    Object *searchEditor;
    Object *tootTimeline;
    Object *wordTypeChooser;
    Object *backButton;
    BOOL    visible;
} SearchBarLayoutData;

ULONG ASM SAVEDS SearchBarLayout_Dispatch(
    REG(a0, Class  *cl),
    REG(a2, Object *o),
    REG(a1, Msg     msg));

static void setBounds(Object *child, WORD l, WORD t, WORD w, WORD h)
{
    struct Gadget *gad = G(child);
    gad->LeftEdge = l;
    gad->TopEdge  = t;
    gad->Width    = (w > 0) ? w : 1;
    gad->Height   = (h > 0) ? h : 1;
}

/* Query a child's GM_DOMAIN(MINIMUM); either out pointer may be NULL. */
static void getChildDomain(Object *child, struct GadgetInfo *gi,
                            WORD *outW, WORD *outH)
{
    struct gpDomain dm;
    dm.MethodID        = GM_DOMAIN;
    dm.gpd_GInfo       = gi;
    dm.gpd_RPort       = NULL;
    dm.gpd_Which       = GDOMAIN_MINIMUM;
    dm.gpd_Domain.Left = 0;
    dm.gpd_Domain.Top  = 0;
    dm.gpd_Domain.Width  = 1;
    dm.gpd_Domain.Height = 1;
    dm.gpd_Attrs       = 0;
    DoMethodA(child, (Msg)&dm);
    if (outW) *outW = dm.gpd_Domain.Width;
    if (outH) *outH = dm.gpd_Domain.Height;
}

/* ------------------------------------------------------------------ */
/* OM_NEW                                                               */
/* ------------------------------------------------------------------ */

static ULONG SearchBarLayout_OnNew(Class *cl, Object *o, struct opSet *msg)
{
    SearchBarLayoutData *inst;
    Object           *newObj;
    struct TagItem   *state;
    struct TagItem   *tag;
    UWORD             childIdx = 0;

    newObj = (Object *)DoSuperMethodA(cl, o, (APTR)msg);
    if (!newObj) return 0;

    inst                  = (SearchBarLayoutData *)INST_DATA(cl, newObj);
    inst->searchEditor    = NULL;
    inst->tootTimeline    = NULL;
    inst->wordTypeChooser = NULL;
    inst->backButton      = NULL;
    inst->visible         = FALSE;  /* hidden until VIEWMODE_Search is entered */

    state = msg->ops_AttrList;
    while ((tag = NextTagItem(&state)) != NULL) {
        if (tag->ti_Tag == LAYOUT_AddChild) {
            if (childIdx == 0)      inst->searchEditor    = (Object *)tag->ti_Data;
            else if (childIdx == 1) inst->tootTimeline    = (Object *)tag->ti_Data;
            else if (childIdx == 2) inst->wordTypeChooser = (Object *)tag->ti_Data;
            else if (childIdx == 3) inst->backButton      = (Object *)tag->ti_Data;
            childIdx++;
        } else if (tag->ti_Tag == SBLAYOUT_Visible) {
            inst->visible = (BOOL)tag->ti_Data;
        }
    }

    return (ULONG)newObj;
}

/* ------------------------------------------------------------------ */
/* OM_DISPOSE                                                           */
/* ------------------------------------------------------------------ */

static ULONG SearchBarLayout_OnDispose(Class *cl, Object *o, Msg msg)
{
    return DoSuperMethodA(cl, o, (APTR)msg);
}

/* ------------------------------------------------------------------ */
/* OM_SET / OM_UPDATE -- tracks SBLAYOUT_Visible only; does NOT itself   */
/* trigger a relayout (see fs3esearchbar.h -- caller must RethinkLayout) */
/* ------------------------------------------------------------------ */

static ULONG SearchBarLayout_OnSet(Class *cl, Object *o, struct opSet *msg)
{
    SearchBarLayoutData *inst = (SearchBarLayoutData *)INST_DATA(cl, o);
    struct TagItem *tag = FindTagItem(SBLAYOUT_Visible, msg->ops_AttrList);
    ULONG result = DoSuperMethodA(cl, o, (APTR)msg);

    if (tag) inst->visible = (BOOL)tag->ti_Data;

    return result;
}

/* ------------------------------------------------------------------ */
/* GM_DOMAIN                                                            */
/* ------------------------------------------------------------------ */

static ULONG SearchBarLayout_OnDomain(Class *cl, Object *o, struct gpDomain *msg)
{
    SearchBarLayoutData *inst   = (SearchBarLayoutData *)INST_DATA(cl, o);
    struct IBox         *domain = &msg->gpd_Domain;
    WORD ttlMinW = 1, ttlMinH = 1, sbMinH = 0;

    (void)cl;

    if (inst->tootTimeline)
        getChildDomain(inst->tootTimeline, msg->gpd_GInfo, &ttlMinW, &ttlMinH);
    if (inst->searchEditor)
        getChildDomain(inst->searchEditor, msg->gpd_GInfo, NULL, &sbMinH);

    /* The row's height comes from the one-line search editor alone, NOT
     * from the chooser's own reported GM_DOMAIN height: chooser.gadget's
     * minimum-height query isn't reliable through this NULL-RPort probe
     * (it reports something far too tall, which used to make the whole
     * row -- and this domain's Height -- balloon to swallow the entire
     * window, leaving tootTimeline nothing). The chooser is stretched to
     * match the editor's height in GM_LAYOUT below regardless of what it
     * asked for. */

    switch (msg->gpd_Which) {
        case GDOMAIN_MAXIMUM:
            domain->Width  = 32767;
            domain->Height = 32767;
            break;
        case GDOMAIN_MINIMUM:
        case GDOMAIN_NOMINAL:
        default:
            /* The search row isn't counted when hidden -- it can shrink to
             * nothing, so it shouldn't inflate the window's minimum size
             * while not shown. The chooser sits beside the editor in the
             * same row (not stacked), so it never adds to the height. */
            domain->Width  = ttlMinW;
            domain->Height = (WORD)(ttlMinH + (inst->visible ? sbMinH : 0));
            break;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* GM_LAYOUT                                                            */
/* ------------------------------------------------------------------ */

static ULONG SearchBarLayout_OnLayout(Class *cl, Object *o, struct gpLayout *msg)
{
    SearchBarLayoutData *inst = (SearchBarLayoutData *)INST_DATA(cl, o);
    WORD left = G(o)->LeftEdge;
    WORD top  = G(o)->TopEdge;
    WORD w    = G(o)->Width;
    WORD h    = G(o)->Height;
    struct gpLayout childMsg;

    (void)cl;

    if (!inst->searchEditor || !inst->tootTimeline) return 0;

    childMsg.MethodID    = GM_LAYOUT;
    childMsg.gpl_GInfo   = msg->gpl_GInfo;
    childMsg.gpl_Initial = msg->gpl_Initial;

    if (inst->visible) {
        WORD searchH = 0, chooserW = 0, backW = 0, rowH, editorW;

        getChildDomain(inst->searchEditor, msg->gpl_GInfo, NULL, &searchH);
        if (inst->wordTypeChooser) {
            getChildDomain(inst->wordTypeChooser, msg->gpl_GInfo, &chooserW, NULL);
            if (chooserW < SBLAYOUT_CHOOSER_MINW) chooserW = SBLAYOUT_CHOOSER_MINW;
        }
        if (inst->backButton) {
            getChildDomain(inst->backButton, msg->gpl_GInfo, &backW, NULL);
            if (backW < SBLAYOUT_BACK_MINW) backW = SBLAYOUT_BACK_MINW;
        }

        /* Row height is the editor's own minimal (one-line) height only --
         * see the comment in GM_DOMAIN above for why the chooser's own
         * reported height must not be used here. The chooser/back button
         * are simply stretched to that same height below. */
        rowH = searchH;
        if (rowH < 1) rowH = 1;
        if (rowH > h) rowH = h;

        if (chooserW > w) chooserW = w;
        if (backW > (WORD)(w - chooserW)) backW = (WORD)(w - chooserW);
        editorW = (WORD)(w - chooserW - backW);
        if (editorW < 1) editorW = 1;

        /* No gap between any of them -- editor takes the left/most of the
         * row, chooser sits next, back button is flush against the far
         * right edge. */
        setBounds(inst->searchEditor, left, top, editorW, rowH);
        DoMethodA(inst->searchEditor, (Msg)&childMsg);

        if (inst->wordTypeChooser) {
            setBounds(inst->wordTypeChooser, (WORD)(left + w - chooserW - backW), top, chooserW, rowH);
            DoMethodA(inst->wordTypeChooser, (Msg)&childMsg);
        }

        if (inst->backButton) {
            setBounds(inst->backButton, (WORD)(left + w - backW), top, backW, rowH);
            DoMethodA(inst->backButton, (Msg)&childMsg);
        }

        setBounds(inst->tootTimeline, left, (WORD)(top + rowH), w, (WORD)(h - rowH));
        DoMethodA(inst->tootTimeline, (Msg)&childMsg);
    } else {
        /* Park the editor (and chooser/back button) just below the
         * container's bottom edge -- a valid, non-zero rectangle that
         * ends up entirely clipped away, rather than a true zero-size
         * gadget (layout.gadget/intuition don't like those). tootTimeline
         * gets the full height. */
        setBounds(inst->searchEditor, left, (WORD)(top + h), w, 1);
        DoMethodA(inst->searchEditor, (Msg)&childMsg);

        if (inst->wordTypeChooser) {
            setBounds(inst->wordTypeChooser, left, (WORD)(top + h), 1, 1);
            DoMethodA(inst->wordTypeChooser, (Msg)&childMsg);
        }

        if (inst->backButton) {
            setBounds(inst->backButton, left, (WORD)(top + h), 1, 1);
            DoMethodA(inst->backButton, (Msg)&childMsg);
        }

        setBounds(inst->tootTimeline, left, top, w, h);
        DoMethodA(inst->tootTimeline, (Msg)&childMsg);
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* GM_RENDER                                                            */
/* We must NOT chain to layout.gadget's own GM_RENDER here, same reason
 * as GM_LAYOUT above: our children's bounds come entirely from our own
 * GM_LAYOUT math (layout.gadget's internal bookkeeping never sees it,
 * since we never call super there either), so its own GM_RENDER doesn't
 * know to skip the hidden search editor -- it would render it at
 * whatever position its own (unused) layout pass thinks is right,
 * regardless of the off-window position GM_LAYOUT actually gave it.
 * That's what made "hide by moving off-window" not actually hide it.
 * Owning GM_RENDER outright fixes it: simply don't forward the message
 * to the search editor or chooser at all while inst->visible is FALSE.
 * The children always exactly tile [left,top,w,h] with no gap in either
 * state (see GM_LAYOUT above), so there's nothing else to erase here --
 * tootTimeline's own full-bounds repaint (it always gets the full
 * container rect when the editor/chooser are hidden) covers whatever
 * they last drew there. */
/* ------------------------------------------------------------------ */

static ULONG SearchBarLayout_OnRender(Class *cl, Object *o, struct gpRender *msg)
{
    SearchBarLayoutData *inst = (SearchBarLayoutData *)INST_DATA(cl, o);
    struct gpRender childMsg;

    (void)cl;

    if (!inst->tootTimeline) return 0;

    childMsg.MethodID   = GM_RENDER;
    childMsg.gpr_GInfo  = msg->gpr_GInfo;
    childMsg.gpr_RPort  = msg->gpr_RPort;
    childMsg.gpr_Redraw = msg->gpr_Redraw;

    if (inst->visible && inst->searchEditor)
        DoMethodA(inst->searchEditor, (Msg)&childMsg);

    if (inst->visible && inst->wordTypeChooser)
        DoMethodA(inst->wordTypeChooser, (Msg)&childMsg);

    if (inst->visible && inst->backButton)
        DoMethodA(inst->backButton, (Msg)&childMsg);

    DoMethodA(inst->tootTimeline, (Msg)&childMsg);

    return 0;
}

/* ------------------------------------------------------------------ */
/* Dispatcher                                                           */
/* ------------------------------------------------------------------ */

ULONG ASM SAVEDS SearchBarLayout_Dispatch(
    REG(a0, Class  *cl),
    REG(a2, Object *o),
    REG(a1, Msg     msg))
{
    switch (msg->MethodID) {
        case OM_NEW:
            return SearchBarLayout_OnNew(cl, o, (struct opSet *)msg);
        case OM_DISPOSE:
            return SearchBarLayout_OnDispose(cl, o, msg);
        case OM_SET:
        case OM_UPDATE:
            return SearchBarLayout_OnSet(cl, o, (struct opSet *)msg);
        case GM_DOMAIN:
            return SearchBarLayout_OnDomain(cl, o, (struct gpDomain *)msg);
        case GM_LAYOUT:
            return SearchBarLayout_OnLayout(cl, o, (struct gpLayout *)msg);
        case GM_RENDER:
            return SearchBarLayout_OnRender(cl, o, (struct gpRender *)msg);
        default:
            return DoSuperMethodA(cl, o, (APTR)msg);
    }
}

/* ------------------------------------------------------------------ */
/* Class init / exit                                                    */
/* ------------------------------------------------------------------ */

Class *SearchBarLayoutClass = NULL;

int SearchBarLayout_Init(void)
{
    SearchBarLayoutClass = MakeClass(
        NULL, NULL, LAYOUT_GetClass(),
        sizeof(SearchBarLayoutData), 0);
    if (!SearchBarLayoutClass) return 0;

    SearchBarLayoutClass->cl_Dispatcher.h_Entry    = (HOOKFUNC)SearchBarLayout_Dispatch;
    SearchBarLayoutClass->cl_Dispatcher.h_SubEntry = NULL;
    SearchBarLayoutClass->cl_Dispatcher.h_Data     = NULL;

    return 1;
}

void SearchBarLayout_Exit(void)
{
    if (SearchBarLayoutClass) {
        FreeClass(SearchBarLayoutClass);
        SearchBarLayoutClass = NULL;
    }
}
