#ifndef FS3EMANAGEURL_H
#define FS3EMANAGEURL_H

/*
 * fs3emanageurl.h - single entry point for "the user clicked a plain
 * website URL" toot hot zones (TTL_HOT_URL body-text links, TTL_HOT_CARD
 * link-preview cards -- see their callers in friendsh3ep.c), covering the
 * action most other clickable toot zones already have and these two
 * didn't.
 *
 * Acts according to app->settings.urlLinkAction (fs3esettings.h):
 *
 *   FS3E_URLLINK_ASK       -- EasyRequestArgs "Open with ?" offering
 *                             "Copy to Clipboard" / "Open with OpenURL".
 *   FS3E_URLLINK_OPENURL   -- straight to openurl.library, no dialog.
 *   FS3E_URLLINK_CLIPBOARD -- straight to a clipboard copy, no dialog.
 *
 * openurl.library is optional (OpenURLBase, declared in friendsh3ep.c --
 * NULL if the library isn't installed): every path above that would use
 * it degrades to a clipboard copy instead when it isn't available, rather
 * than silently doing nothing.
 */

#include <exec/types.h>

/* Acts on url per app->settings.urlLinkAction -- see this header's top
 * comment. No-op if url is NULL/empty. */
void ManageUrl(const char *url);

/* ASL file-save requester (defaulting to app->settings.downloadPath and
 * url's own final path segment as the filename), then a background
 * FS3ENETQ_FETCH_IMAGE download straight to the confirmed path, tracked
 * in the Network downloads window -- the same mechanism ManageUrl()
 * already uses for a "Direct download .zip,.lha" body-text URL, exported
 * here so fs3emediaview.c's "Download full video..." menu action (a
 * click, not a URL-kind check) can trigger the identical flow for a
 * video/GIFV attachment's real file URL. No-op if url is NULL/empty, if
 * it ends in '/' (nothing to name the file), if asl.library isn't
 * available, or if the requester is cancelled. */
void ManageUrl_DownloadToFile(const char *url);

#endif /* FS3EMANAGEURL_H */
