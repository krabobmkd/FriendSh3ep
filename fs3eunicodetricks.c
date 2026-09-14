/*
 * fs3eunicodetricks.c - see fs3eunicodetricks.h.
 *
 * All three tricks share the same shape: decode utf8In into a flat array of
 * Unicode codepoints, remap each one through a per-trick table (anything
 * with no mapping passes through as-is), optionally reorder the array
 * (UpsideDown only), then re-encode to UTF-8. Codepoint count can only ever
 * shrink or stay the same relative to byte count, so the intermediate
 * codepoint buffer is sized off strlen(utf8In) -- a safe, if slightly
 * generous, upper bound.
 */

#include <string.h>
#include <proto/exec.h>

#include "fs3eunicodetricks.h"

/* Decodes one UTF-8 codepoint starting at p (a NUL-terminated string, so
 * p[0] is always readable). *outLen receives the number of bytes consumed
 * (always >= 1, even for a malformed/truncated sequence -- callers advance
 * by *outLen unconditionally). Continuation bytes are validated for shape
 * (0x80..0xBF) so a truncated multi-byte sequence at the very end of the
 * string can't read past the NUL: falls back to treating the lead byte as
 * a raw Latin-1 codepoint in that case, same "don't crash, don't hang"
 * spirit as fs3etootview.c's own Utf8CodepointCount. */
static ULONG DecodeUTF8Codepoint(const unsigned char *p, ULONG *outLen)
{
    unsigned char c = p[0];

    if (c < 0x80) {
        *outLen = 1;
        return c;
    }
    if ((c & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
        *outLen = 2;
        return ((ULONG)(c & 0x1F) << 6) | (p[1] & 0x3F);
    }
    if ((c & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        *outLen = 3;
        return ((ULONG)(c & 0x0F) << 12) | ((ULONG)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    }
    if ((c & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
        *outLen = 4;
        return ((ULONG)(c & 0x07) << 18) | ((ULONG)(p[1] & 0x3F) << 12) |
               ((ULONG)(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
    }

    *outLen = 1;
    return c;
}

/* Encodes codepoint cp as UTF-8 into out (caller guarantees >= 4 bytes
 * room, the worst case). Returns the number of bytes written. */
static ULONG EncodeUTF8Codepoint(ULONG cp, char *out)
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* Mathematical Alphanumeric Symbols block (U+1D400-U+1D7FF): each style
 * is 26 uppercase + 26 lowercase codepoints in a row, plus (for a handful
 * of styles, including Bold and Monospace) its own run of 10 digits. */
static ULONG BoldMap(ULONG cp)
{
    if (cp >= 'A' && cp <= 'Z') return 0x1D400 + (cp - 'A');
    if (cp >= 'a' && cp <= 'z') return 0x1D41A + (cp - 'a');
    if (cp >= '0' && cp <= '9') return 0x1D7CE + (cp - '0');
    return cp;
}

static ULONG BoldSansSerifMap(ULONG cp)
{
    if (cp >= 'A' && cp <= 'Z') return 0x1D5D4 + (cp - 'A');
    if (cp >= 'a' && cp <= 'z') return 0x1D5EE + (cp - 'a');
    if (cp >= '0' && cp <= '9') return 0x1D7EC + (cp - '0');
    return cp;
}

static ULONG MonospaceMap(ULONG cp)
{
    if (cp >= 'A' && cp <= 'Z') return 0x1D670 + (cp - 'A');
    if (cp >= 'a' && cp <= 'z') return 0x1D68A + (cp - 'a');
    if (cp >= '0' && cp <= '9') return 0x1D7F6 + (cp - '0');
    return cp;
}

/* No digit sub-range exists for Italic in this block -- digits fall
 * through to the final `return cp` unchanged, unlike Bold/Monospace
 * above. Lowercase 'h' is left unmapped (falls through to `return cp`
 * too) rather than substituting U+210E (see this map's own doc comment
 * in fs3eunicodetricks.h): tested against the fonts bundled with
 * FriendSh3ep and U+210E has no glyph in them, so it drew as tofu --
 * plain "h" reads better than a missing-glyph box, same "better than
 * nothing" tradeoff UpsideDownMap already makes for B/Q. */
static ULONG ItalicMap(ULONG cp)
{
    if (cp >= 'A' && cp <= 'Z') return 0x1D434 + (cp - 'A');
    if (cp >= 'a' && cp <= 'z' && cp != 'h') return 0x1D44E + (cp - 'a');
    return cp;
}

/* Upside-down look-alikes, the same table classic "flip text" generators
 * use: real turned/rotated Latin letters where Unicode has one (IPA
 * Extensions, Latin Extended-B/D, ...), otherwise the closest existing
 * symbol that reads as upside-down at a glance (e.g. Greek Lambda for V,
 * UP TACK for T), and self-mapped where the glyph is roughly
 * rotation-symmetric already (H, I, N, O, S, X, Z, 0, 8). No good
 * upside-down glyph exists at all for a few letters (B, Q) -- left
 * unmapped, same "better than nothing" tradeoff every such generator
 * makes.
 *
 * Uppercase A-Z is lowercased first and run through the very same
 * lowercase table: dedicated upside-down capitals exist in Unicode
 * (Ɐ, Ⅎ, ⅁, Ԁ, ᴚ, Ꞁ, ...) but they're scattered across obscure blocks
 * (Cyrillic, Canadian Aboriginal, letterlike symbols, ...) the bundled
 * Amiga fonts don't cover -- lots of tofu in testing. The lowercase
 * look-alikes above are mostly plain ASCII or common IPA Extensions
 * characters, both of which those fonts actually have glyphs for. */
static ULONG UpsideDownMap(ULONG cp)
{
    if (cp >= 'A' && cp <= 'Z') cp += ('a' - 'A');

    switch (cp) {
        /* lowercase */
        case 'a': return 0x0250; /* ɐ */
        case 'b': return 'q';
        case 'c': return 0x0254; /* ɔ */
        case 'd': return 'p';
        case 'e': return 0x01DD; /* ǝ */
        case 'f': return 0x025F; /* ɟ */
        case 'g': return 0x0183; /* ƃ */
        case 'h': return 0x0265; /* ɥ */
        /* Turned "i" (U+1D09) sits in the Phonetic Extensions block, which
         * the bundled Amiga fonts don't cover -- renders as tofu. '!' is a
         * plain-ASCII stand-in that's a genuinely good visual match: flip
         * an "i" (dot above a stroke) and you get a dot below a stroke,
         * exactly "!"'s shape. */
        case 'i': return '!';
        case 'j': return 0x027E; /* ɾ */
        case 'k': return 0x029E; /* ʞ */
        case 'l': return 'l';
        case 'm': return 0x026F; /* ɯ */
        case 'n': return 'u';
        case 'o': return 'o';
        case 'p': return 'd';
        case 'q': return 'b';
        case 'r': return 0x0279; /* ɹ */
        case 's': return 's';
        case 't': return 0x0287; /* ʇ */
        case 'u': return 'n';
        case 'v': return 0x028C; /* ʌ */
        case 'w': return 0x028D; /* ʍ */
        case 'x': return 'x';
        case 'y': return 0x028E; /* ʎ */
        case 'z': return 'z';

        /* digits */
        case '1': return 0x0197; /* Ɩ */
        case '2': return 0x3132; /* ᄅ */
        case '3': return 0x0190; /* Ɛ */
        case '4': return 0x3123; /* ㄣ */
        case '6': return '9';
        case '7': return 0x3125; /* ㄥ */
        case '9': return '6';

        /* punctuation */
        case '.': return 0x02D9; /* ˙ */
        case ',': return '\'';
        case '\'': return ',';
        case '!': return 0x00A1; /* ¡ */
        case '?': return 0x00BF; /* ¿ */
        case '(': return ')';
        case ')': return '(';
        case '[': return ']';
        case ']': return '[';
        case '{': return '}';
        case '}': return '{';
        case '<': return '>';
        case '>': return '<';
        case '&': return 0x214B; /* ⅋ */
        case '_': return 0x203E; /* ‾ */

        default: return cp;
    }
}

typedef ULONG (*FS3EUnicodeMapFunc)(ULONG cp);

/* Shared worker: decode utf8In to codepoints, run each through mapFunc,
 * reverse the array if reverseOrder, re-encode. Returns an AllocVec'd
 * UTF-8 string (caller FreeVec()s), or NULL on NULL input/empty
 * string/allocation failure. */
static char *ApplyMap(const char *utf8In, FS3EUnicodeMapFunc mapFunc, BOOL reverseOrder)
{
    ULONG srcLen, count, i, outAlloc, outLen;
    ULONG *codepoints;
    char *out;
    const unsigned char *p;

    if (!utf8In || !utf8In[0]) return NULL;

    srcLen = (ULONG)strlen(utf8In);
    codepoints = (ULONG *)AllocVec(srcLen * sizeof(ULONG), MEMF_ANY);
    if (!codepoints) return NULL;

    p = (const unsigned char *)utf8In;
    count = 0;
    while (*p) {
        ULONG consumed;
        ULONG cp = DecodeUTF8Codepoint(p, &consumed);
        codepoints[count++] = mapFunc(cp);
        p += consumed;
    }

    if (reverseOrder) {
        ULONG lo = 0, hi = count ? count - 1 : 0;
        while (lo < hi) {
            ULONG tmp = codepoints[lo];
            codepoints[lo] = codepoints[hi];
            codepoints[hi] = tmp;
            lo++; hi--;
        }
    }

    outAlloc = count * 4 + 1; /* worst case: every codepoint needs 4 bytes */
    out = (char *)AllocVec(outAlloc, MEMF_ANY);
    if (!out) {
        FreeVec(codepoints);
        return NULL;
    }

    outLen = 0;
    for (i = 0; i < count; i++)
        outLen += EncodeUTF8Codepoint(codepoints[i], out + outLen);
    out[outLen] = '\0';

    FreeVec(codepoints);
    return out;
}

char *FS3EUnicodeTricks_Bold(const char *utf8In)
{
    return ApplyMap(utf8In, BoldMap, FALSE);
}

char *FS3EUnicodeTricks_BoldSansSerif(const char *utf8In)
{
    return ApplyMap(utf8In, BoldSansSerifMap, FALSE);
}

char *FS3EUnicodeTricks_Monospace(const char *utf8In)
{
    return ApplyMap(utf8In, MonospaceMap, FALSE);
}

char *FS3EUnicodeTricks_Italic(const char *utf8In)
{
    return ApplyMap(utf8In, ItalicMap, FALSE);
}

char *FS3EUnicodeTricks_UpsideDown(const char *utf8In)
{
    return ApplyMap(utf8In, UpsideDownMap, TRUE);
}
