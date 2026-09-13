#ifndef FS3EUNICODETRICKS_H
#define FS3EUNICODETRICKS_H

/*
 * fs3eunicodetricks.h - "fake styled text" tricks for FriendSh3ep's Toot
 * window (fs3etootview.c's "Unicode Tricks" menu).
 *
 * Mastodon toots are plain text -- there's no bold/italic markup a server
 * will render for you. The classic workaround social apps use is to swap
 * ordinary ASCII letters/digits for Unicode codepoints that already LOOK
 * bold/monospaced/upside-down in most fonts. Every function here takes a
 * NUL-terminated UTF-8 string (typically UTED_GetSelectedText's result) and
 * returns an AllocVec'd NUL-terminated UTF-8 copy with that substitution
 * applied; non-ASCII input bytes (already-decorated text, accented
 * letters, emoji, ...) pass through unchanged. NULL in yields NULL out;
 * NULL is also returned on allocation failure. Caller must FreeVec() a
 * non-NULL result.
 */

#include <exec/types.h>

/* Uppercase/lowercase Latin letters and digits -> their "Mathematical Bold"
 * look-alikes (Unicode block U+1D400-U+1D7FF, the same block web/social
 * apps' "bold text" generators use). Serif-styled -- looks like a bold
 * book/Times face. */
char *FS3EUnicodeTricks_Bold(const char *utf8In);

/* Same idea as FS3EUnicodeTricks_Bold, different sub-block of the same
 * Mathematical Alphanumeric Symbols range: "Mathematical Sans-Serif Bold"
 * look-alikes. Reads like a bold Arial/Helvetica face instead of a bold
 * serif one -- e.g. "n" becomes U+1D5FB here vs U+1D427 for the serif
 * Bold above. */
char *FS3EUnicodeTricks_BoldSansSerif(const char *utf8In);

/* Same block, "Mathematical Monospace" look-alikes -- reads like fixed-width
 * "Courrier" text even though the gadget's own font never changes. */
char *FS3EUnicodeTricks_Monospace(const char *utf8In);

/* Maps ASCII letters/digits/punctuation to upside-down Unicode look-alikes
 * (IPA Extensions / Latin Extended-B / Unicode symbols -- the same trick
 * "flip text" generators use, e.g. U+0259 LATIN SMALL LETTER SCHWA stands
 * in for an upside-down "e") AND reverses codepoint order, so the whole
 * selection reads correctly when flipped upside down, not just character
 * by character. Uppercase letters are lowercased before mapping -- fonts
 * bundled with FriendSh3ep have much better coverage of the lowercase
 * look-alikes (mostly plain ASCII/IPA) than of dedicated upside-down
 * capitals, which live in obscure, rarely-covered Unicode blocks. */
char *FS3EUnicodeTricks_UpsideDown(const char *utf8In);

#endif /* FS3EUNICODETRICKS_H */
