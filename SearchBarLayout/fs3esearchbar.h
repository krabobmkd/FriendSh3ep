#ifndef FS3ESEARCHBAR_H
#define FS3ESEARCHBAR_H

/*
 * SearchBarLayout - layout.gadget subclass wrapping Part C of the
 * FriendSh3ep main window: a one-line search UniTextEditor stacked above
 * TootTimeline.
 *
 * Exactly 4 children, added via LAYOUT_AddChild in this order:
 *   0  search word editor (UniTextEditor, one-line mode)
 *   1  tootTimeline (TootTimelineClass)
 *   2  word/people type chooser (chooser.gadget, "Word"/"People")
 *   3  back button (push button, GID_SEARCH_BACK_BUTTON)
 *
 * layout.gadget has no notion of a hidden child, so hiding/showing the
 * search row is simulated in GM_LAYOUT: when SBLAYOUT_Visible is FALSE the
 * editor, chooser and back button are all parked just below the container's
 * bottom edge (still a valid, non-zero rectangle -- just entirely clipped
 * away) and tootTimeline gets the full height; when TRUE the row at the top
 * is split horizontally with no gap between any of them -- the back button
 * gets its own natural (minimum) width flush against the far right edge,
 * the chooser sits to its left with its own natural width, the editor gets
 * whatever width remains -- and tootTimeline gets the rest of the height
 * below it.
 *
 * Setting SBLAYOUT_Visible only updates internal state -- it does NOT
 * relayout by itself. Callers must follow it with
 *   RethinkLayout((struct Gadget *)o, window, NULL, TRUE)
 * to actually recompute/redraw just this subtree (see fs3e_setViewMode in
 * friendsh3ep.c), instead of forcing a full window relayout.
 */

#include <exec/types.h>
#include <intuition/classusr.h>

#define SBLAYOUT_Base        (TAG_USER | 0x53540UL)
#define SBLAYOUT_Visible     (SBLAYOUT_Base + 0)  /* BOOL: show/hide the search editor row */

extern Class *SearchBarLayoutClass;

int  SearchBarLayout_Init(void);
void SearchBarLayout_Exit(void);

#endif /* FS3ESEARCHBAR_H */
