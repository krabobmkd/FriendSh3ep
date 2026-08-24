/*
 * fs3eoslocale.c - AmigaOS preferred-language detection (locale.library).
 *
 * See fs3eoslocale.h. Uses OpenLocale(NULL)/CloseLocale() (V38+,
 * locale.doc: "Passing a NULL instead of a name causes this function to
 * return the current default locale... you are guaranteed a valid
 * return"), reading loc_PrefLanguages[0] (falling back to
 * loc_LanguageName) -- the user's single most-preferred language, matching
 * how a browser's Accept-Language / navigator.language works, which is
 * the same comparison Mastodon's own web client makes for its own
 * "translate this toot" prompt.
 *
 * Language-name -> ISO 639 mapping: the AmigaOS NDK (locale.doc,
 * libraries/locale.h) documents loc_PrefLanguages[10] as "the ordered list
 * of preferred languages" but does NOT enumerate the actual set of name
 * strings that appear there. The CONFIRMED entries below are the literal
 * catalog names (loc_LanguageName / loc_PrefLanguages[] value = the
 * .language filename minus its extension) from a real AmigaOS 3.9
 * LOCALE:Languages/ directory listing, plus "english"/"english_british"
 * (English has no catalog file -- it's the OS's built-in default, see
 * OC_BuiltInLanguage). The UNCONFIRMED entries further down are best-effort
 * guesses for languages not present in that listing.
 *
 * These strings are ISO-8859-1 (Latin-1) on real AmigaOS, NOT UTF-8 -- this
 * source file itself is UTF-8, so every accented name below is written with
 * explicit \x.. byte escapes for the raw Latin-1 code point rather than the
 * accented character typed directly (which would be encoded as multiple
 * UTF-8 bytes here and would never match what OpenLocale() actually
 * returns). Matched against a PREFIX of the (ASCII-lowercased) name rather
 * than requiring a full exact match, both to tolerate trailing variants
 * (e.g. "portugu\xeas-brasil" vs "portugu\xeas") and minor spelling
 * differences in the unconfirmed guesses.
 */

#include "fs3eoslocale.h"

#include <string.h>
#include <proto/locale.h>
#include <libraries/locale.h>

extern struct LocaleBase *LocaleBase; /* opened/closed in friendsh3ep.c */

typedef struct {
    const char *prefix; /* ASCII-lowercase, Latin-1 (\x..) for accented bytes */
    const char *code;   /* ISO 639 */
} FS3EOSLocaleEntry;

static const FS3EOSLocaleEntry s_localeMap[] = {
    /* ---- Confirmed (real AmigaOS 3.9 LOCALE:Languages/ listing) ---- */
    { "english",                          "en" }, /* USA English -- built-in default, no file */
    { "english_british",                  "en" }, /* British English */
    { "deutsch",                          "de" },
    { "fran\xe7" "ais",                   "fr" }, /* français */
    { "espa\xf1" "ol",                    "es" }, /* español */
    { "italiano",                         "it" },
    { "portugu\xea" "s",                  "pt" }, /* português (prefix also matches
                                                    * "português-brasil" -- see
                                                    * portugu\xeas-brasil.language) */
    { "nederlands",                       "nl" },
    { "svenska",                          "sv" },
    { "dansk",                            "da" },
    { "norsk",                            "no" },
    { "t\xfc" "rk\xe7" "e",               "tr" }, /* türkçe */
    { "russian",                          "ru" },
    { "czech",                            "cs" },
    { "slovak",                           "sk" },
    { "slovensko",                        "sk" }, /* slovak.language and slovensko.language
                                                    * are two distinct files in the real
                                                    * listing -- likely two translations of
                                                    * the same language (Slovak), not Slovak
                                                    * vs Slovenian; mapped the same either way. */
    { "srpski",                           "sr" }, /* also covers srpski.language.no_patch --
                                                    * loc_LanguageName never carries the
                                                    * ".no_patch" suffix (that's a second
                                                    * catalog FILE for the same language, not
                                                    * a distinct selectable language name) */
    { "hrvatski",                         "hr" },
    { "bosanski",                         "bs" },
    { "catal\xe0",                        "ca" }, /* català */
    { "farsi",                            "fa" },
    { "nihongo",                          "ja" },

    /* ---- Unconfirmed best-effort guesses (not in the real listing above --
     * extend/correct these as more real catalog names turn up). ASCII-only:
     * no accented native name is known/confirmed for these, so guessing one
     * would risk being byte-for-byte wrong in a way that silently never
     * matches (same failure mode this table exists to avoid). ---- */
    { "greek",     "el" },
    { "ellenika",  "el" },
    { "hungar",    "hu" },
    { "magyar",    "hu" },
    { "romanian",  "ro" },
    { "romana",    "ro" },
    { "polski",    "pl" },
    { "polish",    "pl" },
    { "suomi",     "fi" },
    { "finnish",   "fi" },
    { "chinese",   "zh" },
    { "korean",    "ko" },
};

static char s_languageCode[8] = "";
static BOOL s_initialized = FALSE;

/* ASCII-only lowercase, byte-for-byte otherwise -- non-ASCII (accented)
 * Latin-1 bytes are already lowercase in every real catalog name (they
 * match this file's own \x.. escapes verbatim) and are left untouched. */
static void FS3EOSLocale_AsciiLower(char *dst, const char *src, ULONG dstSize)
{
    ULONG i;
    for (i = 0; i + 1 < dstSize && src[i]; i++) {
        char c = src[i];
        dst[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    dst[i] = '\0';
}

void FS3EOSLocale_Init(void)
{
    struct Locale *loc;

    s_languageCode[0] = '\0';
    s_initialized = TRUE;

    if (!LocaleBase) return;

    loc = OpenLocale(NULL);
    if (!loc) return;

    {
        const char *name = loc->loc_PrefLanguages[0];
        if (!name || !name[0]) name = loc->loc_LanguageName;

        if (name && name[0]) {
            char lower[40];
            ULONG i;
            FS3EOSLocale_AsciiLower(lower, name, sizeof(lower));

            for (i = 0; i < sizeof(s_localeMap) / sizeof(s_localeMap[0]); i++) {
                ULONG plen = (ULONG)strlen(s_localeMap[i].prefix);
                if (strncmp(lower, s_localeMap[i].prefix, plen) == 0) {
                    strncpy(s_languageCode, s_localeMap[i].code, sizeof(s_languageCode) - 1);
                    s_languageCode[sizeof(s_languageCode) - 1] = '\0';
                    break;
                }
            }
        }
    }

    CloseLocale(loc);
}

const char *FS3EOSLocale_LanguageCode(void)
{
    if (!s_initialized) FS3EOSLocale_Init();
    return s_languageCode;
}
