#ifndef FS3EOSLOCALE_H
#define FS3EOSLOCALE_H

/*
 * fs3eoslocale.h - AmigaOS preferred-language detection (locale.library).
 *
 * Maps the user's OS-level preferred language (Locale preferences,
 * loc_PrefLanguages[]) to an ISO 639 code, so toot content in a different
 * language can be offered a "Translate" button (see TTL_HOT_TRANSLATE in
 * TootTimeline/fs3etoottimeline.h and FS3EApp_MapStatusToPostSetup in
 * fs3erequests.c).
 */

#include <exec/types.h>

/* Detects and caches the OS language code once. Safe to call more than
 * once (re-detects each time); call once at startup (see main() in
 * friendsh3ep.c). Uses the LocaleBase already opened globally in
 * friendsh3ep.c -- a no-op (leaves the code "") if that failed to open. */
void FS3EOSLocale_Init(void);

/* ISO 639 code (e.g. "en", "fr") of the OS's most-preferred language, or ""
 * if locale.library is unavailable or the language name isn't in this
 * module's best-effort mapping table (see fs3eoslocale.c -- the exact set
 * of loc_PrefLanguages[] strings AmigaOS uses is not enumerated in the
 * NDK, so this table is necessarily incomplete). Lazily calls
 * FS3EOSLocale_Init() itself if it hasn't run yet. */
const char *FS3EOSLocale_LanguageCode(void);

#endif /* FS3EOSLOCALE_H */
