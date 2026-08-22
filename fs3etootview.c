/*
 * fs3etootview.c - "New toot" sub-window layout for FriendSh3ep.
 *
 * See fs3etootview.h. Pattern adapted from EmojiGear/egsearchbox.c.
 */

#include <string.h>
#include <stdio.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/alib.h>

#include <proto/layout.h>
#include <gadgets/layout.h>

#include <proto/button.h>
#include <gadgets/button.h>

#include <proto/unitexteditor.h>
#include <gadgets/unitexteditor.h>

#include <proto/unibutton.h>
#include <gadgets/unibutton.h>

#include <proto/chooser.h>
#include <gadgets/chooser.h>

#include <proto/getfile.h>
#include <gadgets/getfile.h>

#include <proto/checkbox.h>
#include <gadgets/checkbox.h>

#include <proto/label.h>
#include <images/label.h>

#include <proto/window.h>
#include <classes/window.h>

#include <proto/gadtools.h>
#include <libraries/gadtools.h>

#include <proto/dos.h>
#include <dos/dos.h>

#include <intuition/icclass.h>

#include "fs3etootview.h"
#include "fs3eboopsimainwindow.h"
#include "fs3eboopsimessage.h"
#include "fs3egadgetid.h"
#include "fs3elocale.h"
#include "fs3enetworkhelper.h"

#include "friendsh3ep.h"
#include "network_fs3e/fs3enet.h"

extern struct Library *ChooserBase;
extern struct Library *GadToolsBase;

/* attachMediaGF's GETFILE_Pattern -- images, plus audio/video, matching the
 * extensions FS3ETootView_CheckAttachment() below recognizes; no leading
 * dot needed, same convention as fs3ethemeview.c's font pickers ("#?(ttf|
 * otf)"): #? already swallows everything up to and
 * including it. */
#define FS3ETOOT_ATTACH_MEDIA_PATTERN "#?(gif|jpeg|jpg|png|mp3|ogg|mpg|mp4)"

/* nm_UserData values for the window-local "Toot" menu -- see
 * FS3ETootView_MenuCreate/FS3ETootView_HandleInput's WMHI_MENUPICK case.
 * Deliberately its own tiny local scheme rather than fs3emenu.c's
 * FS3EACTION_ / ACTION_UD encoding: every one of these actions is entirely
 * local to this window's own editors, never triggered from anywhere else
 * (unlike the main menu's items, which mirror title-bar buttons/actions),
 * so there's no shared action table to hook into. 0 is reserved (title
 * items carry no UserData). */
typedef enum {
    FS3ETMENU_CLEAR = 1,
    FS3ETMENU_UNDO,
    FS3ETMENU_REDO,
    FS3ETMENU_CUT,
    FS3ETMENU_COPY,
    FS3ETMENU_PASTE,
    FS3ETMENU_EMOJIBOX
} FS3ETootMenuID;

/* Builds and attaches the "Toot" menu (Clear/Undo/Redo/separator/Cut/
 * Copy UTF8/Paste UTF8/separator/Emoji Box) to window. Amiga-Z/Y/X/C/V/E
 * work automatically once attached -- GadTools/Intuition itself watches
 * for the Amiga qualifier plus a menu's nm_CommKey letter and delivers it
 * as an ordinary IDCMP_MENUPICK, exactly like a mouse pick of that item;
 * no separate raw-key handling needed (see WA_IDCMP below, IDCMP_MENUPICK
 * must be set for either kind of pick to arrive at all). */
static BOOL FS3ETootView_MenuCreate(FS3ETootView *tv, struct Screen *screen,
                                     struct Window *window)
{
    struct NewMenu nm[11];
    int n = 0;

    if (!tv || !screen || !window || !GadToolsBase) return FALSE;

    tv->menu           = NULL;
    tv->menuVisualInfo = NULL;

    tv->menuVisualInfo = GetVisualInfo(screen, TAG_END);
    if (!tv->menuVisualInfo) return FALSE;

#define ADD(t, l, k, u) do { \
        nm[n].nm_Type          = (t); \
        nm[n].nm_Label         = (STRPTR)(l); \
        nm[n].nm_CommKey       = (STRPTR)(k); \
        nm[n].nm_Flags         = 0; \
        nm[n].nm_MutualExclude = 0; \
        nm[n].nm_UserData      = (APTR)(ULONG)(u); \
        n++; \
    } while (0)

    ADD(NM_TITLE, LOC(MSG_TOOTMENU_TOOT),    0,   0);
    ADD(NM_ITEM,  LOC(MSG_TOOTMENU_CLEAR),   0,   FS3ETMENU_CLEAR);
    ADD(NM_ITEM,  LOC(MSG_TOOTMENU_UNDO),    "Z", FS3ETMENU_UNDO);
    ADD(NM_ITEM,  LOC(MSG_TOOTMENU_REDO),    "Y", FS3ETMENU_REDO);
    ADD(NM_ITEM,  NM_BARLABEL,               0,   0);
    ADD(NM_ITEM,  LOC(MSG_TOOTMENU_CUT),     "X", FS3ETMENU_CUT);
    ADD(NM_ITEM,  LOC(MSG_TOOTMENU_COPY),    "C", FS3ETMENU_COPY);
    ADD(NM_ITEM,  LOC(MSG_TOOTMENU_PASTE),   "V", FS3ETMENU_PASTE);
    ADD(NM_ITEM,  NM_BARLABEL,               0,   0);
    ADD(NM_ITEM,  LOC(MSG_TOOTMENU_EMOJIBOX),"E", FS3ETMENU_EMOJIBOX);
    ADD(NM_END,   NULL,                      0,   0);
#undef ADD

    tv->menu = CreateMenus(nm, TAG_END);
    if (!tv->menu) {
        FreeVisualInfo(tv->menuVisualInfo);
        tv->menuVisualInfo = NULL;
        return FALSE;
    }

    if (!LayoutMenus(tv->menu, tv->menuVisualInfo, GTMN_NewLookMenus, TRUE, TAG_END)) {
        FreeMenus(tv->menu);
        FreeVisualInfo(tv->menuVisualInfo);
        tv->menu           = NULL;
        tv->menuVisualInfo = NULL;
        return FALSE;
    }

    SetMenuStrip(window, tv->menu);
    return TRUE;
}

/* Detaches and frees the menu. Safe to call any time, attached or not. */
static void FS3ETootView_MenuClose(FS3ETootView *tv, struct Window *window)
{
    if (!tv) return;

    if (window && tv->menu)
        ClearMenuStrip(window);

    if (tv->menu) {
        FreeMenus(tv->menu);
        tv->menu = NULL;
    }
    if (tv->menuVisualInfo) {
        FreeVisualInfo(tv->menuVisualInfo);
        tv->menuVisualInfo = NULL;
    }
}

static const ULONG visibilityMsgIds[FS3ETOOT_NUM_VISIBILITIES] = {
    MSG_TOOT_VISIBILITY_PUBLIC, MSG_TOOT_VISIBILITY_UNLISTED,
    MSG_TOOT_VISIBILITY_PRIVATE, MSG_TOOT_VISIBILITY_DIRECT
};

/* tv->visibilityMeaning's text -- only for kinds whose visibilityChooser is
 * actually editable (see tootKindConfig's visibilityEditable and
 * FS3ETootView_UpdateVisibilityMeaning). Order must match visibilityMsgIds. */
static const ULONG visibilityMeaningMsgIds[FS3ETOOT_NUM_VISIBILITIES] = {
    MSG_TOOT_VISIBILITY_MEANING_PUBLIC, MSG_TOOT_VISIBILITY_MEANING_UNLISTED,
    MSG_TOOT_VISIBILITY_MEANING_PRIVATE, MSG_TOOT_VISIBILITY_MEANING_DIRECT
};

static const ULONG quotePolicyMsgIds[FS3ETOOT_NUM_QUOTEPOLICIES] = {
    MSG_TOOT_QUOTEPOLICY_PUBLIC, MSG_TOOT_QUOTEPOLICY_FOLLOWERS,
    MSG_TOOT_QUOTEPOLICY_NOBODY
};

/* tv->visibilityMeaning's second line, appended after the visibility
 * meaning (see FS3ETootView_UpdateVisibilityMeaning). Order must match
 * quotePolicyMsgIds. */
static const ULONG quotePolicyMeaningMsgIds[FS3ETOOT_NUM_QUOTEPOLICIES] = {
    MSG_TOOT_QUOTEPOLICY_MEANING_PUBLIC, MSG_TOOT_QUOTEPOLICY_MEANING_FOLLOWERS,
    MSG_TOOT_QUOTEPOLICY_MEANING_NOBODY
};

/* languageChooser's entries -- code is the ISO 639 code sent as Mastodon's
 * `language` status field (see FS3EMastodon_PostStatus), name is what the
 * chooser/popup shows. Plain static English names, NOT run through
 * LOC()/fs3elocale.c: chooser.gadget/label.image are classic AmigaOS3
 * gadgets with no UTF-8 decoding of their own (unlike bodyEditor's
 * UniTextEditor or a toot's own utf8rastport-rendered content), so native-
 * script names (Cyrillic/CJK/Arabic/Devanagari/...) would risk rendering as
 * tofu/garbage here even where they render fine elsewhere in this app --
 * ASCII English names sidestep that entirely, same reasoning
 * searchWordTypeChooser's/visibilityChooser's own plain-ASCII labels
 * already follow. code[0]=='\0' (index 0, "(Unspecified)") means "don't
 * send a language field at all" -- see FS3ETootView_GetLanguage and
 * FS3EMastodon_PostStatus's own "omit rather than send empty" convention
 * for in_reply_to_id/quoted_status_id. Ordering: unspecified first, then
 * roughly by how many Mastodon users each language sees. */
static const struct { const char *code; const char *name; } fs3eTootLanguages[FS3ETOOT_NUM_LANGUAGES] = {
    { "",   "(Unspecified)" },
    { "en", "English" },
    { "fr", "French" },
    { "de", "German" },
    { "es", "Spanish" },
    { "it", "Italian" },
    { "pt", "Portuguese" },
    { "nl", "Dutch" },
    { "pl", "Polish" },
    { "ru", "Russian" },
    { "uk", "Ukrainian" },
    { "ja", "Japanese" },
    { "zh", "Chinese" },
    { "ko", "Korean" },
    { "ar", "Arabic" },
    { "tr", "Turkish" },
    { "sv", "Swedish" },
    { "no", "Norwegian" },
    { "da", "Danish" },
    { "fi", "Finnish" },
    { "cs", "Czech" },
    { "el", "Greek" },
    { "he", "Hebrew" },
    { "hi", "Hindi" },
    { "id", "Indonesian" },
    { "vi", "Vietnamese" },
    { "th", "Thai" },
    { "ro", "Romanian" },
    { "hu", "Hungarian" },
    { "bg", "Bulgarian" },
    { "hr", "Croatian" },
    { "sk", "Slovak" },
    { "sl", "Slovenian" },
    { "sr", "Serbian" },
    { "lt", "Lithuanian" },
    { "lv", "Latvian" },
    { "et", "Estonian" },
    { "ca", "Catalan" },
    { "eu", "Basque" },
    { "gl", "Galician" },
    { "eo", "Esperanto" },
    { "ga", "Irish" },
    { "is", "Icelandic" },
    { "cy", "Welsh" },
    { "fa", "Persian" },
    { "ur", "Urdu" },
    { "bn", "Bengali" },
    { "ta", "Tamil" },
    { "te", "Telugu" },
    { "ml", "Malayalam" },
    { "mr", "Marathi" },
    { "gu", "Gujarati" },
    { "kn", "Kannada" },
    { "pa", "Punjabi" },
    { "sw", "Swahili" },
    { "af", "Afrikaans" },
    { "sq", "Albanian" },
    { "az", "Azerbaijani" },
    { "be", "Belarusian" },
    { "bs", "Bosnian" },
    { "ka", "Georgian" },
    { "hy", "Armenian" },
    { "kk", "Kazakh" },
};

/* pollExpirationChooser's entries -- plain ASCII, not run through LOC(),
 * same "many-item technical value list, no translation needed" reasoning as
 * fs3eTootLanguages above. Index FS3ETOOT_POLL_EXPIRATION_DEFAULT_IDX
 * ("3 days") is what FS3ETootView_SetComposeContext resets the chooser to
 * every time it configures poll mode. */
static const char *fs3eTootPollExpirations[FS3ETOOT_NUM_POLL_EXPIRATIONS] = {
    "30 minutes", "1 hour", "3 hours", "5 hours", "1 day",
    "2 days", "3 days", "5 days", "7 days", "14 days"
};

/* Seconds equivalent of each fs3eTootPollExpirations entry, same order --
 * what FS3ETootView_GetPollExpiresInSeconds actually sends as Mastodon's
 * poll[expires_in]. */
static const ULONG fs3eTootPollExpirationSeconds[FS3ETOOT_NUM_POLL_EXPIRATIONS] = {
    1800, 3600, 10800, 18000, 86400,
    172800, 259200, 432000, 604800, 1209600
};

/* pollMultipleChooser's entries -- index FS3ETOOT_POLL_TYPE_DEFAULT_IDX
 * ("Single choice") is what FS3ETootView_SetComposeContext resets the
 * chooser to every time it configures poll mode, same convention as
 * fs3eTootPollExpirations above. Plain ASCII/LOC()'d either would do here
 * (only 2 short entries, unlike the technical-value lists above) -- kept
 * as a locale string since it's ordinary UI copy, not a value table. */
static const ULONG fs3eTootPollTypeMsgIds[FS3ETOOT_NUM_POLL_TYPES] = {
    MSG_TOOT_POLL_TYPE_SINGLE, MSG_TOOT_POLL_TYPE_MULTIPLE
};

/* Defined below FS3ETootKindConfig/tootKindConfig (needs both); forward-
 * declared here since it's called from FS3ETootView_HandleInput, which
 * comes first in the file. */
static void FS3ETootView_UpdateVisibilityMeaning(FS3ETootView *tv);

/* "-" if maxChars is 0 (not yet confirmed by the server for the active
 * account -- see App.accountMaxChars in friendsh3ep.h), else its decimal
 * string. Used by both charCountLabel sprintf call sites below. */
static void FormatMaxChars(ULONG maxChars, char *buf, ULONG bufSize)
{
    if (maxChars > 0) {
        snprintf(buf, bufSize, "%lu", (unsigned long)maxChars);
    } else {
        strncpy(buf, "-", bufSize - 1);
        buf[bufSize - 1] = '\0';
    }
}

/* A transparent, read-only button used as a layout spacer. */
static Object *Spacer(void)
{
    return (Object *)NewObject(BUTTON_GetClass(), NULL,
        GA_ReadOnly,        TRUE,
        BUTTON_BevelStyle,  BVS_NONE,
        BUTTON_Transparent, TRUE,
        TAG_END);
}

BOOL FS3ETootView_Create(FS3ETootView *tv, struct URPDrawContext *textDC)
{
    Object *bottomBar;
    Object *choosersCol;
    Object *attachMediaRow;
    Object *attachMediaLabel;
    Object *attachMediaRow2;
    Object *attachMediaLabel2;
    Object *pollExpirationRow;
    Object *sensitiveLabel;
    Object *languageLabel;
    Object *sensitiveLanguageCol;

    int i;

    {
        LONG sl = tv->left, st = tv->top, sw = tv->width, sh = tv->height;
        memset(tv, 0, sizeof(*tv));
        tv->left = sl; tv->top = st; tv->width = sw; tv->height = sh;
    }

    /* ------------------------------------------------------------------ */
    /* Context message: "Creating a new toot" / "Replying to ..." --      */
    /* read-only, no-bevel UniButton, same pattern as fs3eloginview.c's   */
    /* urlInstructLabel. Supports multi-line/Unicode text if ever needed. */
    /* ------------------------------------------------------------------ */
    tv->contextMessage = (Object *)NewObject(UNIBUTTON_GetClass(), NULL,
        UBT_URPDrawContext, (ULONG)textDC,
        GA_ReadOnly,        TRUE,
        UBT_BevelStyle,     BVS_NONE,
        GA_Text,            (ULONG)LOC(MSG_TOOT_CONTEXT_NEW),
        TAG_END);
    if (!tv->contextMessage) return FALSE;

    /* ------------------------------------------------------------------ */
    /* Visibility meaning: one-line, read-only, no-bevel UniButton at the */
    /* very bottom of the outer layout -- see FS3ETootView_UpdateVisibility */
    /* Meaning. Initial text matches visibilityChooser/quotePolicyChooser's */
    /* CHOOSER_Active default of 0 (Public). */
    /* ------------------------------------------------------------------ */
    tv->visibilityMeaning = (Object *)NewObject(UNIBUTTON_GetClass(), NULL,
        UBT_URPDrawContext, (ULONG)textDC,
        GA_ReadOnly,        TRUE,
        UBT_BevelStyle,     BVS_NONE,
        GA_Text,            (ULONG)LOC(MSG_TOOT_VISIBILITY_MEANING_PUBLIC),
        TAG_END);
    if (!tv->visibilityMeaning) return FALSE;

    /* ------------------------------------------------------------------ */
    /* Main body editor - multi-line, word-wrapped, shares the font cache */
    /* ------------------------------------------------------------------ */
    tv->bodyEditor = (Object *)NewObject(UNITEXTEDITOR_GetClass(), NULL,
        GA_ID,                  (ULONG)GID_TOOT_BODY_EDITOR,
        ICA_TARGET,             (ULONG)TargetInstance,
        UTED_KeyMessageMode,    UKM_Internal,
        UTED_InternalRawKey_SendBack,TRUE,
        UTED_BevelStyle,        BVS_FIELD,
        UTED_URPDrawContext,    (ULONG)textDC,
        UTED_TextPen,           1UL,
        UTED_BgPen,             0UL,
        UTED_WordWrap,          TRUE,
        UTED_DisplayInternalVScroll, TRUE,
        UTED_LeftMargin,        2,
        UTED_TopMargin,         3,
        UTED_BottomMargin,      1,
        UTED_LineSpacing,       0,
        TAG_END);
    if (!tv->bodyEditor) return FALSE;

    /* ------------------------------------------------------------------ */
    /* Attach Media row: [getfile][X], between bodyEditor and bottomBar -- */
    /* same [GETFILE][X clear button] pattern as fs3ethemeview.c's font    */
    /* pickers (see makeGetFileGadget/GID_THEMEV_PRIMARY_CLEAR there).     */
    /* ------------------------------------------------------------------ */
    tv->attachMediaGF = (Object *)NewObject(GETFILE_GetClass(), NULL,
        GA_ID,                  (ULONG)GID_TOOT_ATTACH_MEDIA,
        GA_RelVerify,           TRUE,
        GETFILE_TitleText,      (ULONG)LOC(MSG_TOOT_ATTACH_MEDIA),
        GETFILE_RejectIcons,    TRUE,
        GETFILE_DoPatterns,     TRUE,
        GETFILE_Pattern,        (ULONG)FS3ETOOT_ATTACH_MEDIA_PATTERN,
        GETFILE_FilterDrawers,  TRUE,
        GETFILE_ReadOnly,       FALSE,
        GETFILE_FullFileExpand, FALSE,
        TAG_END);
    if (!tv->attachMediaGF) return FALSE;

    tv->attachMediaClearBtn = (Object *)NewObject(BUTTON_GetClass(), NULL,
        GA_ID,        (ULONG)GID_TOOT_ATTACH_MEDIA_CLEAR,
        GA_RelVerify, TRUE,
        GA_Text,      (ULONG)"X",
        TAG_END);
    if (!tv->attachMediaClearBtn) return FALSE;

    /* attachMediaLabel is NOT added as a child of attachMediaRow itself --
     * label.image (LABEL_GetClass()) is an IMAGECLASS object, not a
     * gadgetclass one, so LAYOUT_AddChild (which expects a real BOOPSI
     * gadget) can't render it. It's attached instead via CHILD_Label where
     * attachMediaRow itself is added to tootExtrasLayout below -- the
     * documented, correct way to pair a label with a gadget (or, as here,
     * a whole sub-layout standing in for one), which also aligns every
     * row's label into one column automatically since tootExtrasLayout is
     * a vertical group. */
    attachMediaLabel = (Object *)NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_TOOT_ATTACH_MEDIA), TAG_END);

    attachMediaRow = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
        LAYOUT_BevelStyle,    BVS_NONE,
        LAYOUT_SpaceInner,    FALSE,
        LAYOUT_AddChild,      (ULONG)tv->attachMediaGF,
        CHILD_WeightedWidth,  1,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      (ULONG)tv->attachMediaClearBtn,
        CHILD_WeightedWidth,  0,
        CHILD_WeightedHeight, 0,
        TAG_END);
    if (!attachMediaRow) return FALSE;

    /* Second attach-media row -- identical [getfile][X] pattern, most
     * servers accept more than one attachment per toot. */
    tv->attachMedia2GF = (Object *)NewObject(GETFILE_GetClass(), NULL,
        GA_ID,                  (ULONG)GID_TOOT_ATTACH_MEDIA2,
        GA_RelVerify,           TRUE,
        GETFILE_TitleText,      (ULONG)LOC(MSG_TOOT_ATTACH_MEDIA),
        GETFILE_RejectIcons,    TRUE,
        GETFILE_DoPatterns,     TRUE,
        GETFILE_Pattern,        (ULONG)FS3ETOOT_ATTACH_MEDIA_PATTERN,
        GETFILE_FilterDrawers,  TRUE,
        GETFILE_ReadOnly,       FALSE,
        GETFILE_FullFileExpand, FALSE,
        TAG_END);
    if (!tv->attachMedia2GF) return FALSE;

    tv->attachMedia2ClearBtn = (Object *)NewObject(BUTTON_GetClass(), NULL,
        GA_ID,        (ULONG)GID_TOOT_ATTACH_MEDIA2_CLEAR,
        GA_RelVerify, TRUE,
        GA_Text,      (ULONG)"X",
        TAG_END);
    if (!tv->attachMedia2ClearBtn) return FALSE;

    /* Same CHILD_Label-at-the-parent reasoning as attachMediaLabel above. */
    attachMediaLabel2 = (Object *)NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_TOOT_ATTACH_MEDIA), TAG_END);

    attachMediaRow2 = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
        LAYOUT_BevelStyle,    BVS_NONE,
        LAYOUT_SpaceInner,    FALSE,
        LAYOUT_AddChild,      (ULONG)tv->attachMedia2GF,
        CHILD_WeightedWidth,  1,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      (ULONG)tv->attachMedia2ClearBtn,
        CHILD_WeightedWidth,  0,
        CHILD_WeightedHeight, 0,
        TAG_END);
    if (!attachMediaRow2) return FALSE;

    /* ------------------------------------------------------------------ */
    /* Poll-answer editors, one per possible answer -- each is added        */
    /* directly to tv->pollExtrasLayout below with CHILD_Label attached      */
    /* (same reasoning as attachMediaLabel above: label.image isn't a       */
    /* gadget, so it can't be a LAYOUT_AddChild sibling). No per-row wrapper */
    /* layout needed here, unlike the attach-media rows above -- a poll     */
    /* answer is just the one editor gadget, nothing else to pack beside it.*/
    /* Layout-only for now (see fs3etootview.h's pollOptionEditor comment). */
    /* ------------------------------------------------------------------ */
    {
        static const ULONG pollOptionGadId[FS3ETOOT_NUM_POLL_OPTIONS] = {
            GID_TOOT_POLL_OPTION1, GID_TOOT_POLL_OPTION2,
            GID_TOOT_POLL_OPTION3, GID_TOOT_POLL_OPTION4
        };
        int p;

        for (p = 0; p < FS3ETOOT_NUM_POLL_OPTIONS; p++) {
            snprintf(tv->pollOptionLabelText[p], sizeof(tv->pollOptionLabelText[p]),
                     LOC(MSG_TOOT_POLL_OPTION_FORMAT), p + 1);

            tv->pollOptionLabel[p] = (Object *)NewObject(LABEL_GetClass(), NULL,
                LABEL_Text, (ULONG)tv->pollOptionLabelText[p], TAG_END);
            if (!tv->pollOptionLabel[p]) return FALSE;

            tv->pollOptionEditor[p] = (Object *)NewObject(UNITEXTEDITOR_GetClass(), NULL,
                GA_ID,                  (ULONG)pollOptionGadId[p],
                ICA_TARGET,             (ULONG)TargetInstance,
                UTED_InternalRawKey_SendBack,TRUE,
                UTED_KeyMessageMode,    UKM_Internal,
                UTED_BevelStyle,        BVS_FIELD,
                UTED_URPDrawContext,    (ULONG)textDC,
                UTED_TextPen,           1UL,
                UTED_BgPen,             0UL,
                UTED_MaxDisplayLines,   1UL,
                UTED_NoLineFeed,        TRUE,
                UTED_WordWrap,          TRUE,
                UTED_LeftMargin,        2,
                UTED_TopMargin,         3,
                UTED_BottomMargin,      1,
                UTED_LineSpacing,       0,
                TAG_END);
            if (!tv->pollOptionEditor[p]) return FALSE;
        }
    }

    /* ------------------------------------------------------------------ */
    /* Poll expiration + type row -- last pollExtrasLayout row, two         */
    /* [label][popup chooser] pairs side by side in their own horizontal    */
    /* sub-layout (same [label][gadget]-pair-per-child shape attachMediaRow */
    /* uses, just with two pairs instead of one gadget + clear button).     */
    /* Expiration options in fs3eTootPollExpirations; default reset to      */
    /* FS3ETOOT_POLL_EXPIRATION_DEFAULT_IDX ("3 days") every time            */
    /* FS3ETootView_SetComposeContext configures poll mode (see below).     */
    /* Type ("Single choice"/"Multiple choice" -- Mastodon's poll[multiple]) */
    /* defaults to FS3ETOOT_POLL_TYPE_DEFAULT_IDX ("Single choice") the      */
    /* same way. ------------------------------------------------------- */
    NewList(&tv->pollExpirationList);
    for (i = 0; i < FS3ETOOT_NUM_POLL_EXPIRATIONS; i++) {
        struct Node *node = NULL;
        if (ChooserBase)
            node = AllocChooserNode(CNA_Text, (ULONG)fs3eTootPollExpirations[i], TAG_END);
        tv->pollExpirationNodes[i] = node;
        if (node) AddTail(&tv->pollExpirationList, node);
    }

    tv->pollExpirationLabel = (Object *)NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_TOOT_POLL_EXPIRATION), TAG_END);

    tv->pollExpirationChooser = (Object *)NewObject(CHOOSER_GetClass(), NULL,
        GA_ID,          (ULONG)GID_TOOT_POLL_EXPIRATION,
        GA_RelVerify,   TRUE,
        ICA_TARGET,     (ULONG)TargetInstance,
        CHOOSER_PopUp,  TRUE,
        CHOOSER_Labels, (ULONG)&tv->pollExpirationList,
        CHOOSER_Active, (ULONG)FS3ETOOT_POLL_EXPIRATION_DEFAULT_IDX,
        TAG_END);
    if (!tv->pollExpirationChooser) return FALSE;

    NewList(&tv->pollMultipleList);
    for (i = 0; i < FS3ETOOT_NUM_POLL_TYPES; i++) {
        struct Node *node = NULL;
        if (ChooserBase)
            node = AllocChooserNode(CNA_Text, (ULONG)LOC(fs3eTootPollTypeMsgIds[i]), TAG_END);
        tv->pollMultipleNodes[i] = node;
        if (node) AddTail(&tv->pollMultipleList, node);
    }

    tv->pollMultipleLabel = (Object *)NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_TOOT_POLL_TYPE), TAG_END);

    tv->pollMultipleChooser = (Object *)NewObject(CHOOSER_GetClass(), NULL,
        GA_ID,          (ULONG)GID_TOOT_POLL_TYPE,
        GA_RelVerify,   TRUE,
        ICA_TARGET,     (ULONG)TargetInstance,
        CHOOSER_PopUp,  TRUE,
        CHOOSER_Labels, (ULONG)&tv->pollMultipleList,
        CHOOSER_Active, (ULONG)FS3ETOOT_POLL_TYPE_DEFAULT_IDX,
        TAG_END);
    if (!tv->pollMultipleChooser) return FALSE;

    pollExpirationRow = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
        LAYOUT_BevelStyle,    BVS_NONE,
        LAYOUT_SpaceInner,    FALSE,
        LAYOUT_AddChild,      (ULONG)tv->pollExpirationChooser,
            CHILD_Label,          (ULONG)tv->pollExpirationLabel,
            CHILD_WeightedWidth,  1,
        LAYOUT_AddChild,      (ULONG)tv->pollMultipleChooser,
            CHILD_Label,          (ULONG)tv->pollMultipleLabel,
            CHILD_WeightedWidth,  1,
        TAG_END);
    if (!pollExpirationRow) return FALSE;

    /* ------------------------------------------------------------------ */
    /* tootExtrasLayout/pollExtrasLayout: two independent, ordinary vertical */
    /* layout.gadget sub-groups -- one holds the two attach-media rows, the */
    /* other the four poll-answer editors plus the expiration chooser, each */
    /* paired with its label via CHILD_Label -- the documented way to      */
    /* attach a label.image to a gadget, and what aligns every row's label  */
    /* into one column since these are vertical groups. Only one of the two */
    /* groups is ever attached to tv->extrasLayout (the "slot", see below)  */
    /* at a time; the other sits detached, kept alive solely by tv's own    */
    /* pointer to it. Both are built with CHILD_NoDispose (see below where  */
    /* each is added to the slot) so layout.gadget never auto-disposes      */
    /* either one on removal or on the window's own teardown --             */
    /* FS3ETootView_Dispose explicitly DisposeObject()s both.               */
    /* ------------------------------------------------------------------ */
    tv->tootExtrasLayout = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle,  BVS_NONE,
        LAYOUT_SpaceOuter,  FALSE,
        LAYOUT_SpaceInner,  FALSE,
        LAYOUT_AddChild,    (ULONG)attachMediaRow,
            CHILD_Label,          (ULONG)attachMediaLabel,
            CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,    (ULONG)attachMediaRow2,
            CHILD_Label,          (ULONG)attachMediaLabel2,
            CHILD_WeightedHeight, 0,
        TAG_END);
    if (!tv->tootExtrasLayout) return FALSE;

    tv->pollExtrasLayout = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle,  BVS_NONE,
        LAYOUT_SpaceOuter,  FALSE,
        LAYOUT_SpaceInner,  FALSE,
        LAYOUT_AddChild,    (ULONG)tv->pollOptionEditor[0],
            CHILD_Label,          (ULONG)tv->pollOptionLabel[0],
            CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,    (ULONG)tv->pollOptionEditor[1],
            CHILD_Label,          (ULONG)tv->pollOptionLabel[1],
            CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,    (ULONG)tv->pollOptionEditor[2],
            CHILD_Label,          (ULONG)tv->pollOptionLabel[2],
            CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,    (ULONG)tv->pollOptionEditor[3],
            CHILD_Label,          (ULONG)tv->pollOptionLabel[3],
            CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,    (ULONG)pollExpirationRow,
            CHILD_WeightedHeight, 0,
        TAG_END);
    if (!tv->pollExtrasLayout) return FALSE;

    /* tv->extrasLayout: the fixed "slot" sitting between bodyEditor and    */
    /* bottomBar in the outer layout below -- an ordinary layout.gadget     */
    /* that holds exactly one child at a time (tootExtrasLayout or          */
    /* pollExtrasLayout), swapped via LAYOUT_RemoveChild/LAYOUT_AddChild in */
    /* FS3ETootView_SetComposeContext. Keeping this slot itself as a        */
    /* permanent, never-swapped child of the outer layout is what keeps the */
    /* outer layout's own child order (contextMessage/bodyEditor/slot/      */
    /* bottomBar/visibilityMeaning) stable -- LAYOUT_AddChild only ever     */
    /* appends, so swapping directly at the outer layout's level would push */
    /* whichever group got re-added after bottomBar/visibilityMeaning.      */
    /* tootExtrasLayout starts attached (toot mode is the default compose   */
    /* kind); the CHILD_NoDispose here is what FS3ETootView_Dispose relies  */
    /* on to be allowed to free both groups itself, see above. */
    tv->extrasLayout = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle,  BVS_NONE,
        LAYOUT_SpaceOuter,  FALSE,
        LAYOUT_SpaceInner,  FALSE,
        LAYOUT_AddChild,    (ULONG)tv->tootExtrasLayout,
            CHILD_WeightedHeight, 0,
            CHILD_NoDispose,      TRUE,
        TAG_END);
    if (!tv->extrasLayout) return FALSE;
    tv->currentExtras = tv->tootExtrasLayout;

    /* ------------------------------------------------------------------ */
    /* Bottom bar: visibility chooser, char count, emoji buttons, Toot     */
    /* ------------------------------------------------------------------ */
    NewList(&tv->visibilityList);
    for (i = 0; i < FS3ETOOT_NUM_VISIBILITIES; i++) {
        struct Node *node = NULL;
        if (ChooserBase)
            node = AllocChooserNode(CNA_Text, (ULONG)LOC(visibilityMsgIds[i]), TAG_END);
        tv->visibilityNodes[i] = node;
        if (node) AddTail(&tv->visibilityList, node);
    }

    tv->visibilityChooser = (Object *)NewObject(CHOOSER_GetClass(), NULL,
        GA_ID,          (ULONG)GID_TOOT_VISIBILITY,
        GA_RelVerify,   TRUE,
        ICA_TARGET,     (ULONG)TargetInstance,
        CHOOSER_PopUp,  TRUE,
        CHOOSER_Labels, (ULONG)&tv->visibilityList,
        CHOOSER_Active, 0UL,
        TAG_END);
    if (!tv->visibilityChooser) return FALSE;

    NewList(&tv->quotePolicyList);
    for (i = 0; i < FS3ETOOT_NUM_QUOTEPOLICIES; i++) {
        struct Node *node = NULL;
        if (ChooserBase)
            node = AllocChooserNode(CNA_Text, (ULONG)LOC(quotePolicyMsgIds[i]), TAG_END);
        tv->quotePolicyNodes[i] = node;
        if (node) AddTail(&tv->quotePolicyList, node);
    }

    tv->quotePolicyChooser = (Object *)NewObject(CHOOSER_GetClass(), NULL,
        GA_ID,          (ULONG)GID_TOOT_QUOTEPOLICY,
        GA_RelVerify,   TRUE,
        ICA_TARGET,     (ULONG)TargetInstance,
        CHOOSER_PopUp,  TRUE,
        CHOOSER_Labels, (ULONG)&tv->quotePolicyList,
        CHOOSER_Active, 0UL,
        TAG_END);
    if (!tv->quotePolicyChooser) return FALSE;

    NewList(&tv->languageList);
    for (i = 0; i < FS3ETOOT_NUM_LANGUAGES; i++) {
        struct Node *node = NULL;
        if (ChooserBase)
            node = AllocChooserNode(CNA_Text, (ULONG)fs3eTootLanguages[i].name, TAG_END);
        tv->languageNodes[i] = node;
        if (node) AddTail(&tv->languageList, node);
    }

    tv->languageChooser = (Object *)NewObject(CHOOSER_GetClass(), NULL,
        GA_ID,          (ULONG)GID_TOOT_LANGUAGE,
        GA_RelVerify,   TRUE,
        ICA_TARGET,     (ULONG)TargetInstance,
        CHOOSER_PopUp,  TRUE,
        CHOOSER_Labels, (ULONG)&tv->languageList,
        CHOOSER_Active, 0UL,
        TAG_END);
    if (!tv->languageChooser) return FALSE;

    {
        char maxBuf[16];
        FormatMaxChars(app->accountMaxChars, maxBuf, sizeof(maxBuf));
        sprintf(tv->charCountText, LOC(MSG_TOOT_CHARS_FORMAT), 0UL, maxBuf);
    }
    tv->charCountLabel = (Object *)NewObject(BUTTON_GetClass(), NULL,
        GA_ReadOnly,          TRUE,
        BUTTON_BevelStyle,    BVS_NONE,
        BUTTON_Justification, BCJ_CENTER,
        GA_Text,              (ULONG)tv->charCountText,
        TAG_END);
    if (!tv->charCountLabel) return FALSE;

    tv->emojiBtn =
     NewObject(UNIBUTTON_GetClass(), NULL,
        GA_ID,           GID_TOOT_EMOJI_BUTTON,
        GA_RelVerify,    TRUE,
        ICA_TARGET,      (ULONG)TargetInstance,
        UBT_BevelStyle,  BVS_BUTTON,
        UBT_URPDrawContext,    (ULONG)textDC,
        GA_Text,         (ULONG) "\xF0\x9F\x98\x8A",
        TAG_END);

    if (!tv->emojiBtn ) return FALSE;

    /* "Sensitive content" -- Mastodon's `sensitive` flag applies to the
     * whole status (see fs3etootview.h's field comment), one checkbox
     * right before tootBtn. */
    tv->sensitiveCheck = (Object *)NewObject(CHECKBOX_GetClass(), NULL,
        GA_ID,        (ULONG)GID_TOOT_SENSITIVE,
        GA_RelVerify, TRUE,
        GA_Selected,  FALSE,
        TAG_END);
    if (!tv->sensitiveCheck) return FALSE;

    sensitiveLabel = (Object *)NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_TOOT_SENSITIVE), TAG_END);

    languageLabel = (Object *)NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_TOOT_LANGUAGE), TAG_END);

    /* Language chooser + "Sensitive content" checkbox stacked vertically in
     * their own sub-column, same "stack instead of widening bottomBar"
     * reasoning as choosersCol above -- this replaces sensitiveCheck's old
     * standalone bottomBar slot. */
    sensitiveLanguageCol = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,  LAYOUT_ORIENT_VERT,
        LAYOUT_AddChild,     (ULONG)tv->languageChooser,
            CHILD_Label,         (ULONG)languageLabel,
        LAYOUT_AddChild,     (ULONG)tv->sensitiveCheck,
            CHILD_Label,         (ULONG)sensitiveLabel,
        TAG_END);
    if (!sensitiveLanguageCol) return FALSE;

    tv->tootBtn = (Object *)NewObject(BUTTON_GetClass(), NULL,
        GA_ID,        (ULONG)GID_TOOT_SEND_BUTTON,
        GA_RelVerify, TRUE,
       // only use GADGETUP ICA_TARGET,   (ULONG)TargetInstance,
        GA_Text,      (ULONG)LOC(MSG_TOOT_SEND),
        /* No account connected yet at all == nothing to post to. Refined
         * below by every FS3ETootView_UpdateSendEnabled() call once app's
         * account state can actually change (login/switch/load). */
        GA_Disabled,  (ULONG)(!(app->accountAccessToken && app->accountAccessToken[0])),
        TAG_END);
    if (!tv->tootBtn) return FALSE;

    /* visibilityChooser/quotePolicyChooser stacked vertically in their own
     * sub-layout instead of sitting side by side in bottomBar -- two
     * Choosers plus the char-count label, emoji button, and toot button
     * all in one horizontal row forced the window too wide. Stacking the
     * two Choosers halves the horizontal space they need, at the cost of
     * bottomBar's row getting a little taller (both Choosers are short
     * gadgets, so this fits comfortably). */
    choosersCol = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,  LAYOUT_ORIENT_VERT,
        LAYOUT_AddChild,     (ULONG)tv->visibilityChooser,
        LAYOUT_AddChild,     (ULONG)tv->quotePolicyChooser,
        TAG_END);
    if (!choosersCol) return FALSE;

    bottomBar = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,  LAYOUT_ORIENT_HORIZ,
        LAYOUT_AddChild,     (ULONG)choosersCol,
            CHILD_WeightedWidth, 0,
        LAYOUT_AddChild,     (ULONG)tv->charCountLabel,
            CHILD_WeightedWidth, 0,
        LAYOUT_AddChild,     (ULONG)tv->emojiBtn,
            CHILD_WeightedWidth, 0,
        LAYOUT_AddChild,     (ULONG)Spacer(),
            CHILD_WeightedWidth, 1,
        LAYOUT_AddChild,     (ULONG)sensitiveLanguageCol,
            CHILD_WeightedWidth, 0,
        LAYOUT_AddChild,     (ULONG)tv->tootBtn,
            CHILD_WeightedWidth, 0,
        TAG_END);

    /* ------------------------------------------------------------------ */
    /* Outer vertical layout                                              */
    /* ------------------------------------------------------------------ */
    tv->layout = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
                LAYOUT_SpaceOuter,  TRUE,
        LAYOUT_SpaceInner,  TRUE,
        LAYOUT_AddChild,    (ULONG)tv->contextMessage,
            CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,    (ULONG)tv->bodyEditor,
            CHILD_WeightedHeight, 1,
        LAYOUT_AddChild,    (ULONG)tv->extrasLayout,
            CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,    (ULONG)bottomBar,
            CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,    (ULONG)tv->visibilityMeaning,
            CHILD_WeightedHeight, 0,

        TAG_END);
    if (!tv->layout) return FALSE;

    tv->windowObj = (Object *)NewObject(WINDOW_GetClass(), NULL,
        WA_Left,   100,
        WA_Top,    60,
        WA_Width,  420,
        WA_Height, 260,
        WA_IDCMP,  IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_NEWSIZE |
                   IDCMP_MENUPICK | IDCMP_RAWKEY,
        WA_Flags,  WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET |
                   WFLG_SIZEGADGET | WFLG_SIZEBRIGHT | WFLG_SIZEBBOTTOM |
                   WFLG_ACTIVATE | WFLG_SMART_REFRESH,
        WA_Title,  (ULONG)LOC(MSG_TOOT_TITLE),
        WINDOW_ParentGroup, (ULONG)tv->layout,
        TAG_END);
    if (!tv->windowObj) {
        DisposeObject(tv->layout);
        tv->layout = NULL;
        return FALSE;
    }

    return TRUE;
}

void FS3ETootView_Dispose(FS3ETootView *tv)
{
    int i;

    if (!tv) return;

    if (tv->composePostId) {
        FreeVec(tv->composePostId);
        tv->composePostId = NULL;
    }
    for (i = 0; i < FS3ETOOT_MAX_MEDIA; i++) {
        if (tv->composeMediaIds[i]) {
            FreeVec(tv->composeMediaIds[i]);
            tv->composeMediaIds[i] = NULL;
        }
    }
    tv->composeMediaCount = 0;

    if (tv->windowObj) {
        FS3ETootView_Close(tv);
        DisposeObject(tv->windowObj);
        tv->windowObj = NULL;

        /* Both were added to tv->extrasLayout (now gone, cascaded from
         * windowObj above) with CHILD_NoDispose TRUE -- see the "slot"
         * design comment in FS3ETootView_Create -- so neither was destroyed
         * by that cascade, whether or not it was the one still attached.
         * They're entirely tv's own responsibility to free. */
        if (tv->tootExtrasLayout) {
            DisposeObject(tv->tootExtrasLayout);
            tv->tootExtrasLayout = NULL;
        }
        if (tv->pollExtrasLayout) {
            DisposeObject(tv->pollExtrasLayout);
            tv->pollExtrasLayout = NULL;
        }
        tv->currentExtras = NULL;
    }

    if (ChooserBase) {
        for (i = 0; i < FS3ETOOT_NUM_VISIBILITIES; i++) {
            if (tv->visibilityNodes[i]) {
                FreeChooserNode(tv->visibilityNodes[i]);
                tv->visibilityNodes[i] = NULL;
            }
        }
        for (i = 0; i < FS3ETOOT_NUM_QUOTEPOLICIES; i++) {
            if (tv->quotePolicyNodes[i]) {
                FreeChooserNode(tv->quotePolicyNodes[i]);
                tv->quotePolicyNodes[i] = NULL;
            }
        }
        for (i = 0; i < FS3ETOOT_NUM_LANGUAGES; i++) {
            if (tv->languageNodes[i]) {
                FreeChooserNode(tv->languageNodes[i]);
                tv->languageNodes[i] = NULL;
            }
        }
        for (i = 0; i < FS3ETOOT_NUM_POLL_EXPIRATIONS; i++) {
            if (tv->pollExpirationNodes[i]) {
                FreeChooserNode(tv->pollExpirationNodes[i]);
                tv->pollExpirationNodes[i] = NULL;
            }
        }
        for (i = 0; i < FS3ETOOT_NUM_POLL_TYPES; i++) {
            if (tv->pollMultipleNodes[i]) {
                FreeChooserNode(tv->pollMultipleNodes[i]);
                tv->pollMultipleNodes[i] = NULL;
            }
        }
    }
}

void FS3ETootView_Open(FS3ETootView *tv)
{
    if (!tv || !tv->windowObj) return;

    if (tv->window) {
        WindowToFront(tv->window);
        ActivateWindow(tv->window);
  //no need    DoMethod(tv->windowObj, WM_RETHINK, NULL);
        return; /* already open */
    }

    /* Every fresh open starts the visibility chooser back at Public,
     * regardless of whatever was last picked -- Action_NewToot's menu path
     * also resets composeKind to NEW right before this, but the chooser
     * itself is reset here so it's consistent across every open path. */
    if (tv->visibilityChooser) {
        SetAttrs(tv->visibilityChooser, CHOOSER_Active, 0UL, TAG_END);
        FS3ETootView_UpdateVisibilityMeaning(tv);
    }
    /* Same reasoning for the quote-policy chooser: every fresh open starts
     * back at Everybody. */
    if (tv->quotePolicyChooser) {
        SetAttrs(tv->quotePolicyChooser, CHOOSER_Active, 0UL, TAG_END);
        FS3ETootView_UpdateVisibilityMeaning(tv);
    }
    /* Unlike visibility/quote-policy above, the language pick is NOT reset
     * to "(Unspecified)" here -- it's restored from app->settings.tootLanguage
     * (persisted across sessions, see FS3EApp_SubmitToot's own comment on
     * where that gets updated), so a user who always toots in the same
     * language doesn't have to reselect it on every single compose. Falls
     * back to index 0 if the saved code isn't found in fs3eTootLanguages
     * (empty/unset, or a code this build's table doesn't carry). */
    if (tv->languageChooser) {
        ULONG idx = 0, i;
        if (app->settings.tootLanguage && app->settings.tootLanguage[0]) {
            for (i = 0; i < FS3ETOOT_NUM_LANGUAGES; i++) {
                if (strcmp(fs3eTootLanguages[i].code, app->settings.tootLanguage) == 0) {
                    idx = i;
                    break;
                }
            }
        }
        SetAttrs(tv->languageChooser, CHOOSER_Active, idx, TAG_END);
    }
    /* Same reasoning again: a leftover attachment from a previous, unrelated
     * toot shouldn't silently carry over into this one. */
    if (tv->attachMediaGF) {
        SetAttrs(tv->attachMediaGF,
                 GETFILE_File,   (ULONG)"",
                 GETFILE_Drawer, (ULONG)"",
                 TAG_END);
    }
    if (tv->attachMedia2GF) {
        SetAttrs(tv->attachMedia2GF,
                 GETFILE_File,   (ULONG)"",
                 GETFILE_Drawer, (ULONG)"",
                 TAG_END);
    }
    /* Same reasoning again: don't carry over a previous toot's flag. */
    if (tv->sensitiveCheck) {
        SetAttrs(tv->sensitiveCheck, GA_Selected, FALSE, TAG_END);
    }
    /* Same reasoning again: a leftover poll answer from a previous, unrelated
     * poll shouldn't silently carry over into this one. */
    {
        int p;
        for (p = 0; p < FS3ETOOT_NUM_POLL_OPTIONS; p++) {
            if (tv->pollOptionEditor[p])
                SetAttrs(tv->pollOptionEditor[p], UTED_Text, (ULONG)"", TAG_END);
        }
    }

    if (CurrentMainScreen) {
        SetAttrs(tv->windowObj,
                 WA_CustomScreen, (ULONG)CurrentMainScreen,
                 TAG_END);
    }

    if (tv->width > 0) {
        SetAttrs(tv->windowObj,
                 WA_Left,   (ULONG)tv->left,
                 WA_Top,    (ULONG)tv->top,
                 WA_Width,  (ULONG)tv->width,
                 WA_Height, (ULONG)tv->height,
                 TAG_END);
    }
    if(tv->emojiBtn)
    {
        /* need to be font updated */
        SetAttrs(tv->emojiBtn, GA_Text,(ULONG) "\xF0\x9F\x98\x8A",TAG_END);
    }

    tv->window = (struct Window *)DoMethod(tv->windowObj, WM_OPEN, NULL);

    /* Menu strips attach to the transient struct Window, not the
     * persistent BOOPSI Object * -- must be (re)built every open. */
    if (tv->window)
        FS3ETootView_MenuCreate(tv, tv->window->WScreen, tv->window);

    FS3ETootView_UpdateCharCount(tv);
    FS3ETootView_UpdateSendEnabled(tv);

    /* activate editor so it receive keyboard immediately */
    if(tv->window)
    {
        ActivateGadget(tv->bodyEditor,tv->window,NULL);
    }
    DoMethod(tv->windowObj, WM_RETHINK, NULL);
}

void FS3ETootView_Close(FS3ETootView *tv)
{
    /* closing the toot view closes the emojibox view */
    FS3EEmojiBoxWindow_Close(&app->emojiBoxWindow);

    if (!tv || !tv->windowObj || !tv->window) return;

    /* Detach the menu before the window itself goes away. */
    FS3ETootView_MenuClose(tv, tv->window);

    GetAttr(WA_Left,   tv->windowObj, (ULONG *)&tv->left);
    GetAttr(WA_Top,    tv->windowObj, (ULONG *)&tv->top);
    GetAttr(WA_Width,  tv->windowObj, (ULONG *)&tv->width);
    GetAttr(WA_Height, tv->windowObj, (ULONG *)&tv->height);

    DoMethod(tv->windowObj, WM_CLOSE, NULL);
    tv->window = NULL;
}


void FS3ETootView_ClearText(FS3ETootView *tv)
{
    if(!tv) return;
    if (tv->bodyEditor)
    {
        if(tv->window)
            SetGadgetAttrs((struct Gadget *)tv->bodyEditor,
                tv->window, NULL,
                UTED_SelectAll,       TRUE,
                UTED_DeleteSelection, TRUE,
                TAG_DONE);
        else
            SetAttrs((struct Gadget *)tv->bodyEditor,
                UTED_SelectAll,       TRUE,
                UTED_DeleteSelection, TRUE,
                TAG_DONE);
    }
    FS3ETootView_UpdateCharCount(tv);
}
BOOL FS3ETootView_HandleInput(FS3ETootView *tv)
{
    ULONG result;
    ULONG refreshFlags = 0;
 //   ULONG reactivateEditor=FALSE;
 int reactvalue = 3;
    if (!tv || !tv->windowObj) return FALSE;
    if (!tv->window) return TRUE; /* closed, that's fine */



    while ((result = DoMethod(tv->windowObj, WM_HANDLEINPUT, NULL))
           != WMHI_LASTMSG)
    {
        switch (result & WMHI_CLASSMASK)
        {
            case WMHI_CLOSEWINDOW:
                FS3ETootView_Close(tv);

                return TRUE;

            case WMHI_NEWSIZE:
                /* contextMessage is a UniButton, not an editor -- layout.gadget
                 * handles its resize like any other button (see
                 * fs3eloginview.c's urlInstructLabel, same reasoning). */
                if (tv->bodyEditor)
                    RefreshGList((struct Gadget *)tv->bodyEditor, tv->window, NULL, 1);
                {
                    int i;
                    for( i=0 ; i<FS3ETOOT_NUM_POLL_OPTIONS ;i++)
                    {
                        if( tv->pollOptionEditor[i] )
                        {
                            RefreshGList((struct Gadget *)tv->pollOptionEditor[i], tv->window, NULL, 1);
                        }
                    }
                }

                break;

            case WMHI_GADGETUP:
            {
                ULONG gadId = result & WMHI_GADGETMASK;
                if (gadId == GID_TOOT_BODY_EDITOR)
                    FS3ETootView_UpdateCharCount(tv);
                else if (gadId == GID_TOOT_VISIBILITY || gadId == GID_TOOT_QUOTEPOLICY)
                    FS3ETootView_UpdateVisibilityMeaning(tv);
                else if (gadId == GID_TOOT_ATTACH_MEDIA) {
                    gfRequestFile(tv->attachMediaGF, tv->window);
                } else if (gadId == GID_TOOT_ATTACH_MEDIA_CLEAR) {
                    SetGadgetAttrs((struct Gadget *)tv->attachMediaGF,
                                   tv->window, NULL,
                                   GETFILE_File,   (ULONG)"",
                                   GETFILE_Drawer, (ULONG)"",
                                   TAG_DONE);
                } else if (gadId == GID_TOOT_ATTACH_MEDIA2) {
                    gfRequestFile(tv->attachMedia2GF, tv->window);
                } else if (gadId == GID_TOOT_ATTACH_MEDIA2_CLEAR) {
                    SetGadgetAttrs((struct Gadget *)tv->attachMedia2GF,
                                   tv->window, NULL,
                                   GETFILE_File,   (ULONG)"",
                                   GETFILE_Drawer, (ULONG)"",
                                   TAG_DONE);
                } else if( gadId == GID_TOOT_LANGUAGE)
                {
                    /* Sync app->settings.tootLanguage the instant the
                     * chooser selection changes, not only when a toot is
                     * actually sent -- see that field's doc comment in
                     * fs3esettings.h. Persisted to disk at the next
                     * FS3ESettings_Save() call (app quit, or any other
                     * settings save). */
                    const char *language = FS3ETootView_GetLanguage(tv);
                    if (!app->settings.tootLanguage ||
                        strcmp(app->settings.tootLanguage, language) != 0)
                    {
                        if (app->settings.tootLanguage) FreeVec(app->settings.tootLanguage);
                        app->settings.tootLanguage = NetStrDup(language);
                    }
                }

                BoopsiDelay_BeginMessage(DelayQueue, gadId);
                BoopsiDelay_AddTag(DelayQueue, GA_Selected, 0);
                BoopsiDelay_EndMessage(DelayQueue);
                break;
            }

            case WMHI_MENUPICK:
            {
                UWORD menuCode = (UWORD)(result & WMHI_MENUMASK);
                struct MenuItem *item;
                ULONG udata;

                while (menuCode != MENUNULL) {
                    item = ItemAddress(tv->menu, menuCode);
                    if (!item) break;
                    udata = (ULONG)GTMENUITEM_USERDATA(item);

                    switch ((FS3ETootMenuID)udata) {
                        case FS3ETMENU_CLEAR:
                            FS3ETootView_ClearText(tv);
                            break;

                        case FS3ETMENU_UNDO:
                            if (tv->bodyEditor)
                                SetGadgetAttrs((struct Gadget *)tv->bodyEditor,
                                    tv->window, NULL,
                                    UTED_Undo, TRUE, TAG_DONE);
                            FS3ETootView_UpdateCharCount(tv);
                            tv->reactivateEditor = reactvalue;
                            break;

                        case FS3ETMENU_REDO:
                            if (tv->bodyEditor)
                                SetGadgetAttrs((struct Gadget *)tv->bodyEditor,
                                    tv->window, NULL,
                                    UTED_Redo, TRUE, TAG_DONE);
                            FS3ETootView_UpdateCharCount(tv);
                            tv->reactivateEditor = reactvalue;
                            break;

                        case FS3ETMENU_CUT:
                            if (tv->bodyEditor)
                            {
                                SetGadgetAttrs((struct Gadget *)tv->bodyEditor,
                                    tv->window, NULL,
                                    UTED_ApplyCut, TRUE, TAG_DONE);
                            }
                            FS3ETootView_UpdateCharCount(tv);
                            tv->reactivateEditor = reactvalue;
                            break;

                        case FS3ETMENU_COPY:
                            if (tv->bodyEditor)
                            {
                                SetGadgetAttrs((struct Gadget *)tv->bodyEditor,
                                    tv->window, NULL,
                                    UTED_ApplyCopy, TRUE, TAG_DONE);

                            tv->reactivateEditor = reactvalue;
                            }
                            break;

                        case FS3ETMENU_PASTE:
                            if (tv->bodyEditor)
                            {
                                SetGadgetAttrs((struct Gadget *)tv->bodyEditor,
                                    tv->window, NULL,
                                    UTED_ApplyPaste, TRUE, TAG_DONE);

                                FS3ETootView_UpdateCharCount(tv);

                             tv->reactivateEditor = reactvalue;
                            }
                            break;

                        case FS3ETMENU_EMOJIBOX:
                            FS3EEmojiBoxWindow_Open(&app->emojiBoxWindow);
                            break;

                        default:
                            break; /* title item or unknown -- ignore */
                    }

                    menuCode = item->NextSelect;
                }
                break;
            }

           case WMHI_RAWKEY:
            {
                ULONG key = (result & 0x07f);
                ULONG isUp = (result & 0x080);
               // ULONG qualifiers=0;
               // int keyUsed=0;

                //GetAttr(WINDOW_Qualifier,app->window_obj,&qualifiers);
                if(isUp && key == 0x45)
                {
                    FS3ETootView_Close(tv);
                }
                // if(!isUp && !keyUsed && tv->window)
                // {
                //     // keyUsed = (int) FS3EEmojiBox_HandleFKey(
                //     //   &app->emojiBoxWindow,
                //     // tv->bodyEditor, key, qualifiers,tv->window);
                // }
                // if (!keyUsed && app->activeEditorObj == app->textEditorObj)
                // {
                //     SetGdAttrs(app->textEditorObj,
                //         UTED_PutRawKey,(result & 0x0ff)|(qualifiers<<16),TAG_END);
                // }
            }
            break;

            default:
                break;
        }
    }
    if(tv->reactivateEditor>0 && tv->window && tv->bodyEditor)
    {
        tv->reactivateEditor--;
        ActivateGadget(tv->bodyEditor,tv->window,NULL);
    }

    return TRUE;
}

ULONG FS3ETootView_GetSignalMask(FS3ETootView *tv)
{
    if (!tv || !tv->window) return 0;
    return (1L << tv->window->UserPort->mp_SigBit);
}

void FS3ETootView_GetWindowPos(FS3ETootView *tv)
{
    if (!tv || !tv->windowObj || !tv->window) return;

    GetAttr(WA_Left,   tv->windowObj, (ULONG *)&tv->left);
    GetAttr(WA_Top,    tv->windowObj, (ULONG *)&tv->top);
    GetAttr(WA_Width,  tv->windowObj, (ULONG *)&tv->width);
    GetAttr(WA_Height, tv->windowObj, (ULONG *)&tv->height);
}

static const char *GetEditorUTF8Line(Object *editor, ULONG line)
{
    const char *text = NULL;

    if (!editor) return NULL;

    SetAttrs(editor, UTED_LineTextToGet, line, TAG_END);
    GetAttr(UTED_LineUTF8TextBuffer, editor, (ULONG *)&text);

    return text;
}

/* Counts Unicode codepoints in a NUL-terminated UTF-8 string -- what
 * FS3ETootView_UpdateCharCount uses for the shown "N / Max" count, since
 * that reads far closer to what Mastodon's own server-side limit does than
 * a raw byte count (strlen()) would: an accented letter or CJK character
 * is 2-3 UTF-8 bytes but one codepoint, and counting bytes would flag a
 * toot as over-length long before the server actually would. Not a perfect
 * match either -- Mastodon counts UTF-16 code units, so a codepoint outside
 * the Basic Multilingual Plane (most emoji) costs 2 there but only 1 here
 * -- but codepoints is the simpler, still-far-more-accurate-than-bytes rule
 * to show the user while typing (see the mirrored, TootTimeline-private
 * utf8_codepoints_range() in fs3etoottimeline_private.h for the same
 * algorithm; not reused directly since this module has no dependency on
 * TootTimeline's internals). */
static ULONG Utf8CodepointCount(const char *s)
{
    ULONG n = 0;
    const unsigned char *p = (const unsigned char *)s;

    if (!p) return 0;
    while (*p) {
        unsigned char c = *p;
        if      (c < 0x80) p += 1;
        else if (c < 0xE0) p += 2;
        else if (c < 0xF0) p += 3;
        else               p += 4;
        n++;
    }
    return n;
}

void FS3ETootView_UpdateCharCount(FS3ETootView *tv)
{
    ULONG lineCount = 0, i, total = 0;

    if (!tv || !tv->bodyEditor || !tv->charCountLabel) return;

    GetAttr(UTED_LineCount, tv->bodyEditor, &lineCount);
    for (i = 0; i < lineCount; i++) {
        const char *line = GetEditorUTF8Line(tv->bodyEditor, i);
        if (line) total += Utf8CodepointCount(line);
        if (i + 1 < lineCount) total += 1; /* newline */
    }

    {
        char maxBuf[16];
        FormatMaxChars(app->accountMaxChars, maxBuf, sizeof(maxBuf));
        sprintf(tv->charCountText, LOC(MSG_TOOT_CHARS_FORMAT), (unsigned long)total, maxBuf);
    }
    if (tv->window)
        SetGadgetAttrs((struct Gadget *)tv->charCountLabel, tv->window, NULL,
                       GA_Text, (ULONG)tv->charCountText, TAG_END);
    else
        SetAttrs((Object *)tv->charCountLabel,
                 GA_Text, (ULONG)tv->charCountText, TAG_END);
}

void FS3ETootView_UpdateSendEnabled(FS3ETootView *tv)
{
    BOOL connected, enabled;

    if (!tv || !tv->tootBtn || !app) return;

    connected = (app->accountAccessToken && app->accountAccessToken[0]) ? TRUE : FALSE;
    /* Also disabled while an attachment upload is in flight (see
     * app->tootUploadPending's comment in friendsh3ep.h) -- the actual
     * PUT/POST can't be built yet without the upload's media id, so a
     * second click here would just race the first one. */
    enabled = (BOOL)(connected && !app->tootUploadPending);

    if (tv->window)
        SetGadgetAttrs((struct Gadget *)tv->tootBtn, tv->window, NULL,
                       GA_Disabled, (ULONG)!enabled, TAG_DONE);
    else
        SetAttrs(tv->tootBtn, GA_Disabled, (ULONG)!enabled, TAG_END);
}

/* Everything that varies per FS3ETootKind, kept as one table instead of
 * scattered switch-statements -- adding a future kind (e.g. an actual poll
 * compose UI) means adding one row here, not touching three separate
 * places. FS3ETOOT_KIND_REPLY/QUOTE/MESSAGE's titleMsgId is unused (their
 * title is printf-formatted with the target @handle, not a plain lookup) --
 * handled as an explicit exception in FS3ETootView_SetComposeContext. */
typedef struct FS3ETootKindConfig {
    ULONG titleMsgId;         /* MSG_TOOT_CONTEXT_* for contextMessage's text */
    ULONG buttonMsgId;        /* MSG_TOOT_SEND_* for tootBtn's label */
    BOOL  prefillBody;        /* TRUE = bodyEditor is replaced with params->body */
    BOOL  visibilityEditable; /* FALSE disables visibilityChooser -- Mastodon's
                                * edit endpoint silently ignores visibility
                                * changes, so MODIFY shouldn't imply it works */
    BOOL  pollMode;           /* TRUE swaps tv->extrasLayout's child to pollExtrasLayout
                                * (four poll-answer rows + expiration row) instead of
                                * tootExtrasLayout (the two attach-media rows) -- see
                                * FS3ETootView_SetComposeContext */
} FS3ETootKindConfig;

static const FS3ETootKindConfig tootKindConfig[] = {
    /* FS3ETOOT_KIND_NEW     */ { MSG_TOOT_CONTEXT_NEW,    MSG_TOOT_SEND,       FALSE, TRUE,  FALSE },
    /* FS3ETOOT_KIND_MODIFY  */ { MSG_TOOT_CONTEXT_MODIFY, MSG_TOOT_SEND_MODIFY, TRUE, FALSE, FALSE },
    /* FS3ETOOT_KIND_POLL    */ { MSG_TOOT_CONTEXT_POLL,   MSG_TOOT_SEND,       FALSE, TRUE,  TRUE  },
    /* FS3ETOOT_KIND_REPLY   */ { MSG_TOOT_CONTEXT_NEW /* unused, see above */, MSG_TOOT_SEND_REPLY, TRUE, TRUE, FALSE },
    /* FS3ETOOT_KIND_QUOTE   */ { MSG_TOOT_CONTEXT_NEW /* unused, see above */, MSG_TOOT_SEND_QUOTE, FALSE, TRUE, FALSE },
    /* FS3ETOOT_KIND_MESSAGE */ { MSG_TOOT_CONTEXT_NEW /* unused, see above */, MSG_TOOT_SEND,       TRUE, TRUE, FALSE },
    /* FS3ETOOT_KIND_MODIFY_BIO */ { MSG_TOOT_CONTEXT_MODIFY_BIO, MSG_TOOT_SEND_MODIFY, TRUE, FALSE, FALSE },
};

/* Updates tv->visibilityMeaning's text -- blank for kinds whose
 * visibilityChooser isn't actually editable (MODIFY -- see tootKindConfig),
 * else the meaning of the currently selected visibility. Called after
 * SetComposeContext picks a kind, on every visibility chooser change
 * (GID_TOOT_VISIBILITY in FS3ETootView_HandleInput), and after
 * FS3ETootView_Open resets the chooser back to Public. */
static void FS3ETootView_UpdateVisibilityMeaning(FS3ETootView *tv)
{
    char textBuf[200];
    const char *text;
    const FS3ETootKindConfig *cfg;
    ULONG kindIdx;
    LONG vis, qp;

    if (!tv || !tv->visibilityMeaning) return;

    kindIdx = (ULONG)tv->composeKind;
    if (kindIdx >= sizeof(tootKindConfig) / sizeof(tootKindConfig[0]))
        kindIdx = FS3ETOOT_KIND_NEW;
    cfg = &tootKindConfig[kindIdx];

    if (!cfg->visibilityEditable) {
        text = ""; /* e.g. MODIFY: choosers are disabled, nothing to explain */
    } else {
        vis = FS3ETootView_GetVisibility(tv);
        if (vis < 0 || vis >= FS3ETOOT_NUM_VISIBILITIES) vis = 0;
        qp = FS3ETootView_GetQuotePolicy(tv);
        if (qp < 0 || qp >= FS3ETOOT_NUM_QUOTEPOLICIES) qp = 0;

        snprintf(textBuf, sizeof(textBuf), "%s,\n%s",
                 LOC(visibilityMeaningMsgIds[vis]), LOC(quotePolicyMeaningMsgIds[qp]));
        text = textBuf;
    }

    if (tv->window)
        SetGadgetAttrs((struct Gadget *)tv->visibilityMeaning, tv->window, NULL,
                       GA_Text, (ULONG)text, TAG_DONE);
    else
        SetAttrs(tv->visibilityMeaning, GA_Text, (ULONG)text, TAG_END);
}

void FS3ETootView_SetComposeContext(FS3ETootView *tv, FS3ETootKind kind,
                                     const FS3ETootComposeParams *params)
{
    char textBuf[256];
    const char *text;
    const FS3ETootKindConfig *cfg;
    ULONG kindIdx = (ULONG)kind;

    if (!tv) return;

    if (kindIdx >= sizeof(tootKindConfig) / sizeof(tootKindConfig[0]))
        kindIdx = FS3ETOOT_KIND_NEW;
    cfg = &tootKindConfig[kindIdx];

    tv->composeKind = kind;

    if (tv->composePostId) {
        FreeVec(tv->composePostId);
        tv->composePostId = NULL;
    }
    if (params && params->postId && params->postId[0]) {
        ULONG n = (ULONG)strlen(params->postId) + 1;
        tv->composePostId = AllocVec(n, MEMF_ANY);
        if (tv->composePostId) CopyMem((APTR)params->postId, tv->composePostId, n);
    }

    {
        ULONG i;
        for (i = 0; i < FS3ETOOT_MAX_MEDIA; i++) {
            if (tv->composeMediaIds[i]) {
                FreeVec(tv->composeMediaIds[i]);
                tv->composeMediaIds[i] = NULL;
            }
        }
        tv->composeMediaCount = 0;
        if (params) {
            ULONG mc = params->mediaCount;
            if (mc > FS3ETOOT_MAX_MEDIA) mc = FS3ETOOT_MAX_MEDIA;
            for (i = 0; i < mc; i++) {
                if (params->mediaIds[i] && params->mediaIds[i][0]) {
                    ULONG n = (ULONG)strlen(params->mediaIds[i]) + 1;
                    tv->composeMediaIds[tv->composeMediaCount] = AllocVec(n, MEMF_ANY);
                    if (tv->composeMediaIds[tv->composeMediaCount]) {
                        CopyMem((APTR)params->mediaIds[i], tv->composeMediaIds[tv->composeMediaCount], n);
                        tv->composeMediaCount++;
                    }
                }
            }
        }
    }

    if( kind == FS3ETOOT_KIND_NEW )
    {
        FS3ETootView_ClearText(tv);
    }

    if (kind == FS3ETOOT_KIND_REPLY) {
        snprintf(textBuf, sizeof(textBuf)-1, LOC(MSG_TOOT_CONTEXT_REPLY_FORMAT),
                 (params && params->acct && params->acct[0]) ? params->acct : "?");
        text = textBuf;
    } else if (kind == FS3ETOOT_KIND_QUOTE) {
        snprintf(textBuf, sizeof(textBuf)-1, LOC(MSG_TOOT_CONTEXT_QUOTE_FORMAT),
                 (params && params->acct && params->acct[0]) ? params->acct : "?");
        text = textBuf;
    } else if (kind == FS3ETOOT_KIND_MESSAGE) {
        snprintf(textBuf, sizeof(textBuf)-1, LOC(MSG_TOOT_CONTEXT_MESSAGE_FORMAT),
                 (params && params->acct && params->acct[0]) ? params->acct : "?");
        text = textBuf;
    } else {
        text = LOC(cfg->titleMsgId);
    }

    if (tv->contextMessage) {
        if (tv->window)
            SetGadgetAttrs((struct Gadget *)tv->contextMessage, tv->window, NULL,
                           GA_Text, (ULONG)text, TAG_DONE);
        else
            SetAttrs(tv->contextMessage, GA_Text, (ULONG)text, TAG_END);
    }

    if (tv->tootBtn) {
        const char *btnText = LOC(cfg->buttonMsgId);

        if (tv->window)
            SetGadgetAttrs((struct Gadget *)tv->tootBtn, tv->window, NULL,
                           GA_Text, (ULONG)btnText, TAG_DONE);
        else
            SetAttrs(tv->tootBtn, GA_Text, (ULONG)btnText, TAG_END);
    }

    if (tv->visibilityChooser) {
        if (tv->window)
            SetGadgetAttrs((struct Gadget *)tv->visibilityChooser, tv->window, NULL,
                           GA_Disabled, (ULONG)!cfg->visibilityEditable, TAG_DONE);
        else
            SetAttrs(tv->visibilityChooser, GA_Disabled, (ULONG)!cfg->visibilityEditable, TAG_END);
    }

    /* Swap tv->extrasLayout's (the slot's) one child between
     * tootExtrasLayout (the two attach-media rows) and pollExtrasLayout
     * (the four poll-answer rows + expiration row) -- see the "slot" design
     * comment above tv->extrasLayout's construction in FS3ETootView_Create.
     * Both groups were added there with CHILD_NoDispose TRUE, so removing
     * one here only detaches it (it stays alive, owned by tv->tootExtras/
     * pollExtrasLayout) rather than destroying it.
     *
     * A plain RethinkLayout on the slot isn't enough here: bodyEditor (the
     * slot's sibling, weighted height 1) needs to grow/shrink to absorb
     * whatever height the swap frees up or consumes, and that
     * redistribution only happens if the *outer* layout (tv->layout, the
     * slot's parent) is what gets relayouted, not the slot itself.
     * WM_RETHINK on the whole window is the simplest way to guarantee that,
     * same as every other structural change in this window
     * (FS3ETootView_Open already ends on one). */
    if (tv->extrasLayout && tv->tootExtrasLayout && tv->pollExtrasLayout) {
        Object *want = cfg->pollMode ? tv->pollExtrasLayout : tv->tootExtrasLayout;

        if (tv->currentExtras != want) {
            if (tv->window) {
                SetGadgetAttrs((struct Gadget *)tv->extrasLayout, tv->window, NULL,
                               LAYOUT_RemoveChild, (ULONG)tv->currentExtras, TAG_DONE);
                SetGadgetAttrs((struct Gadget *)tv->extrasLayout, tv->window, NULL,
                               LAYOUT_AddChild,    (ULONG)want,
                               CHILD_WeightedHeight, 0,
                               CHILD_NoDispose,      TRUE,
                               TAG_DONE);
                DoMethod(tv->windowObj, WM_RETHINK, NULL);
            } else {
                SetAttrs(tv->extrasLayout, LAYOUT_RemoveChild, (ULONG)tv->currentExtras, TAG_END);
                SetAttrs(tv->extrasLayout,
                         LAYOUT_AddChild,      (ULONG)want,
                         CHILD_WeightedHeight, 0,
                         CHILD_NoDispose,      TRUE,
                         TAG_END);
            }
            tv->currentExtras = want;
        }
    }

    /* Every time poll mode is (re)configured, the expiration and type
     * choosers go back to their defaults ("3 days" / "Single choice") --
     * same "don't carry over a leftover pick from whatever was open
     * before" reasoning as FS3ETootView_Open's visibility/quote-policy
     * chooser resets, just triggered from SetComposeContext instead since
     * that's the point poll mode is actually (re)entered. */
    if (tv->pollExpirationChooser && cfg->pollMode) {
        if (tv->window)
            SetGadgetAttrs((struct Gadget *)tv->pollExpirationChooser, tv->window, NULL,
                           CHOOSER_Active, (ULONG)FS3ETOOT_POLL_EXPIRATION_DEFAULT_IDX, TAG_DONE);
        else
            SetAttrs(tv->pollExpirationChooser,
                     CHOOSER_Active, (ULONG)FS3ETOOT_POLL_EXPIRATION_DEFAULT_IDX, TAG_END);
    }
    if (tv->pollMultipleChooser && cfg->pollMode) {
        if (tv->window)
            SetGadgetAttrs((struct Gadget *)tv->pollMultipleChooser, tv->window, NULL,
                           CHOOSER_Active, (ULONG)FS3ETOOT_POLL_TYPE_DEFAULT_IDX, TAG_DONE);
        else
            SetAttrs(tv->pollMultipleChooser,
                     CHOOSER_Active, (ULONG)FS3ETOOT_POLL_TYPE_DEFAULT_IDX, TAG_END);
    }

    FS3ETootView_UpdateVisibilityMeaning(tv);

    /* Prefill the body -- only for kinds that declare it, every other kind
     * starts from an empty editor. MODIFY prefills with what's already
     * posted (params->body); REPLY prefills with an "@acct " mention
     * prefix built from params->acct, the standard "who this reply is
     * addressed to" convention every mainstream Mastodon client shows.
     * QUOTE does NOT prefill the quoted text -- quoted_status_id already
     * makes the server (and every quote-aware client) render the quoted
     * toot as its own embedded card; copying its text into the body too
     * would just duplicate it in the posted status' own text. MESSAGE
     * prefills the same "@acct " mention prefix as REPLY -- it's a fresh
     * toot (no in_reply_to_id), but still needs the mention so the target
     * actually sees it in their notifications. */
    if (cfg->prefillBody && tv->bodyEditor) {
        char bodyBuf[300];
        const char *body;

        if (kind == FS3ETOOT_KIND_REPLY || kind == FS3ETOOT_KIND_MESSAGE) {
            snprintf(bodyBuf, sizeof(bodyBuf), "@%s ",
                     (params && params->acct && params->acct[0]) ? params->acct : "");
            body = bodyBuf;
        } else {
            body = (params && params->body) ? params->body : "";
        }

        if (tv->window)
            SetGadgetAttrs((struct Gadget *)tv->bodyEditor, tv->window, NULL,
                           UTED_Text, (ULONG)body, TAG_DONE);
        else
            SetAttrs(tv->bodyEditor, UTED_Text, (ULONG)body, TAG_END);

        FS3ETootView_UpdateCharCount(tv);
    }
}

/* WATCH OUT ! if return not NULL , must be FreeVec()'ed */
const char *FS3ETootView_GetUTF8Body(FS3ETootView *tv)
{
    if (!tv) return NULL;
    const char *p=NULL;
    if(tv->bodyEditor)
    {
        GetAttr(UTED_Text,tv->bodyEditor,(ULONG)&p);
    }
    return p;

    //return GetEditorUTF8Line(tv->bodyEditor, 0);
}

LONG FS3ETootView_GetVisibility(FS3ETootView *tv)
{
    ULONG active = 0;

    if (!tv || !tv->visibilityChooser) return 0;

    GetAttr(CHOOSER_Active, tv->visibilityChooser, &active);
    return (LONG)active;
}

LONG FS3ETootView_GetQuotePolicy(FS3ETootView *tv)
{
    ULONG active = 0;

    if (!tv || !tv->quotePolicyChooser) return 0;

    GetAttr(CHOOSER_Active, tv->quotePolicyChooser, &active);
    return (LONG)active;
}

BOOL FS3ETootView_GetSensitive(FS3ETootView *tv)
{
    ULONG selected = 0;

    if (!tv || !tv->sensitiveCheck) return FALSE;

    GetAttr(GA_Selected, tv->sensitiveCheck, &selected);
    return selected ? TRUE : FALSE;
}

const char *FS3ETootView_GetLanguage(FS3ETootView *tv)
{
    ULONG active = 0;

    if (!tv || !tv->languageChooser) return "";

    GetAttr(CHOOSER_Active, tv->languageChooser, &active);
    if (active >= FS3ETOOT_NUM_LANGUAGES) return "";
    return fs3eTootLanguages[active].code;
}

const char *FS3ETootView_GetPollOption(FS3ETootView *tv, ULONG index)
{
    const char *p = NULL;

    if (!tv || index >= FS3ETOOT_NUM_POLL_OPTIONS || !tv->pollOptionEditor[index])
        return NULL;

    GetAttr(UTED_Text, tv->pollOptionEditor[index], (ULONG *)&p);
    return p;
}

ULONG FS3ETootView_GetPollExpiresInSeconds(FS3ETootView *tv)
{
    ULONG active = 0;

    if (!tv || !tv->pollExpirationChooser)
        return fs3eTootPollExpirationSeconds[FS3ETOOT_POLL_EXPIRATION_DEFAULT_IDX];

    GetAttr(CHOOSER_Active, tv->pollExpirationChooser, &active);
    if (active >= FS3ETOOT_NUM_POLL_EXPIRATIONS)
        return fs3eTootPollExpirationSeconds[FS3ETOOT_POLL_EXPIRATION_DEFAULT_IDX];
    return fs3eTootPollExpirationSeconds[active];
}

BOOL FS3ETootView_GetPollMultiple(FS3ETootView *tv)
{
    ULONG active = 0;

    if (!tv || !tv->pollMultipleChooser) return FALSE;

    GetAttr(CHOOSER_Active, tv->pollMultipleChooser, &active);
    return (active == 1) ? TRUE : FALSE;
}

/* Case-insensitive full-string match -- avoids a Stricmp()/UtilityBase
 * dependency just for this one small check. */
static BOOL ExtEquals(const char *ext, const char *want)
{
    for (; *ext && *want; ext++, want++) {
        char a = *ext;
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (a != *want) return FALSE;
    }
    return (BOOL)(*ext == '\0' && *want == '\0');
}

/* Shared body for FS3ETootView_CheckAttachment/CheckAttachment2 -- gf is
 * whichever attach-media GetFile gadget (attachMediaGF or attachMedia2GF)
 * the caller wants checked. */
static FS3ETootAttachStatus CheckAttachmentGF(Object *gf,
    char *outPath, ULONG outPathSize, const char **outMimeType)
{
    ULONG filePtr = 0, drawerPtr = 0;
    const char *file, *drawer;
    const char *ext;
    BPTR lock;
    struct FileInfoBlock *fib;
    BOOL sizeOk;

    if (!gf || !outPath || outPathSize < 1)
        return FS3ETOOT_ATTACH_NONE;

    GetAttr(GETFILE_File, gf, &filePtr);
    file = (const char *)filePtr;
    if (!file || !file[0])
        return FS3ETOOT_ATTACH_NONE;

    GetAttr(GETFILE_Drawer, gf, &drawerPtr);
    drawer = (const char *)drawerPtr;

    if (drawer && drawer[0]) {
        ULONG dirLen = (ULONG)strlen(drawer);
        BOOL  needSlash = drawer[dirLen - 1] != ':' && drawer[dirLen - 1] != '/';
        snprintf(outPath, (size_t)outPathSize, "%s%s%s", drawer, needSlash ? "/" : "", file);
    } else {
        strncpy(outPath, file, outPathSize - 1);
        outPath[outPathSize - 1] = '\0';
    }

    ext = strrchr(outPath, '.');
    if (!ext) return FS3ETOOT_ATTACH_BADEXT;
    ext++;

         if (ExtEquals(ext, "gif"))                          { if (outMimeType) *outMimeType = "image/gif";  }
    else if (ExtEquals(ext, "jpeg") || ExtEquals(ext, "jpg")) { if (outMimeType) *outMimeType = "image/jpeg"; }
    else if (ExtEquals(ext, "png"))                           { if (outMimeType) *outMimeType = "image/png";  }
    else if (ExtEquals(ext, "mp3"))                           { if (outMimeType) *outMimeType = "audio/mpeg"; }
    else if (ExtEquals(ext, "ogg"))                           { if (outMimeType) *outMimeType = "audio/ogg";  }
    else if (ExtEquals(ext, "mpg"))                           { if (outMimeType) *outMimeType = "video/mpeg"; }
    else if (ExtEquals(ext, "mp4"))                           { if (outMimeType) *outMimeType = "video/mp4";  }
    else return FS3ETOOT_ATTACH_BADEXT;

    lock = Lock((STRPTR)outPath, SHARED_LOCK);
    if (!lock) return FS3ETOOT_ATTACH_MISSING;

    fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    if (!fib) { UnLock(lock); return FS3ETOOT_ATTACH_MISSING; }

    sizeOk = (BOOL)(Examine(lock, fib) && fib->fib_DirEntryType < 0 &&
                    fib->fib_Size > 0 && (ULONG)fib->fib_Size <= FS3ENET_UPLOAD_MAX_BYTES);

    FreeDosObject(DOS_FIB, fib);
    UnLock(lock);

    if (!sizeOk) return FS3ETOOT_ATTACH_TOOBIG;

    return FS3ETOOT_ATTACH_OK;
}

FS3ETootAttachStatus FS3ETootView_CheckAttachment(FS3ETootView *tv,
    char *outPath, ULONG outPathSize, const char **outMimeType)
{
    if (!tv) return FS3ETOOT_ATTACH_NONE;
    return CheckAttachmentGF(tv->attachMediaGF, outPath, outPathSize, outMimeType);
}

FS3ETootAttachStatus FS3ETootView_CheckAttachment2(FS3ETootView *tv,
    char *outPath, ULONG outPathSize, const char **outMimeType)
{
    if (!tv) return FS3ETOOT_ATTACH_NONE;
    return CheckAttachmentGF(tv->attachMedia2GF, outPath, outPathSize, outMimeType);
}
