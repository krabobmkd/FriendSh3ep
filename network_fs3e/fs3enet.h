#ifndef FS3ENET_H
#define FS3ENET_H

/*
 * FriendSh3ep network process - public API.
 *
 * This static library is linked into the GUI executable, but (besides
 * FS3ENet_Start/FS3ENet_Stop, which run on the caller's task) all of its work
 * happens in a separate AmigaDOS process created with CreateNewProc().
 * The GUI and the network process talk only through FS3ENetMessage exchanged
 * over Exec MsgPorts - no shared GUI state, and this library never includes
 * Intuition/BOOPSI/utf8rastport headers.
 *
 * See ../ARCHITECTURE.md for the full design and roadmap.
 */

#include <exec/ports.h>
#include <exec/types.h>

#include "fs3enet_mastodon.h"

/* Request types, sent by the GUI to the network process' request port. */
enum FS3ENetRequestType
{
    FS3ENETQ_SHUTDOWN = 0,   /* ask the network process to exit */
    FS3ENETQ_LOGIN_START,    /* register app, return authorize URL  (Phase 1) */
    FS3ENETQ_LOGIN_FINISH,   /* exchange oauth code for access token (Phase 1) */
    FS3ENETQ_TIMELINE,       /* fetch a timeline page                (Phase 2) */
    FS3ENETQ_POST_STATUS,    /* publish a new status (toot)          (Phase 2) */
    FS3ENETQ_FETCH_IMAGE,    /* fetch/return cached avatar or media   (Phase 2) */
    FS3ENETQ_FLUSH_CACHE,    /* delete every file in the disk cache               */
    FS3ENETQ_SET_CACHE_DIR,  /* live-swap the disk cache directory + max size, no restart --
                               * see FS3ENet_SetCacheDir/FS3ENetSetCacheDirReq below */
    FS3ENETQ_VERIFY_ACCOUNT, /* re-verify an existing access token, backfill account fields */
    FS3ENETQ_FAVORITE,       /* toggle favourite/unfavourite on a status */
    FS3ENETQ_REBLOG,         /* toggle reblog/unreblog (boost) on a status */
    FS3ENETQ_ACCOUNT_LOOKUP, /* resolve an acct string to a full account (profile view) */
    FS3ENETQ_RELATIONSHIP,   /* fetch following/followed-by state for an account id */
    FS3ENETQ_FOLLOW,         /* toggle follow/unfollow on an account */
    FS3ENETQ_INSTANCE_INFO,  /* fetch the server's per-toot character limit */
    FS3ENETQ_INSTANCE_DETAILS, /* fetch a full "about this server" profile for
                               * ANY domain (not just the connected account's own) --
                               * see FS3ENetInstanceDetailsReq/Reply below */
    FS3ENETQ_EDIT_STATUS,    /* edit an existing status' text (own toots only) */
    FS3ENETQ_DELETE_STATUS,  /* delete an existing status (own toots only) */
    FS3ENETQ_UPDATE_BIO,     /* set the connected user's own profile bio (note) */
    FS3ENETQ_UPLOAD_MEDIA,   /* POST /api/v2/media, upload one attachment file --
                               * see FS3ENetUploadMediaReq/Reply below. Fired before
                               * FS3ENETQ_POST_STATUS when composing a toot with an
                               * attachment; the returned media id feeds
                               * FS3ENetPostStatusReq's fs3ep_MediaIds. */
    FS3ENETQ_NOTIFICATIONS,  /* fetch a page of notifications */
    FS3ENETQ_ACCOUNTS_LIST,  /* fetch a list of accounts: search results, or a
                               * user's followers/following -- see
                               * FS3ENetAccountsListReq/Reply below */
    FS3ENETQ_RELATIONSHIPS,  /* batch following/followed-by state for N account
                               * ids at once -- see FS3ENetRelationshipsReq/Reply
                               * below; used to badge account-row list items,
                               * unlike singular FS3ENETQ_RELATIONSHIP which only
                               * ever targets the profile header's one account */
    FS3ENETQ_TRANSLATE_STATUS, /* server-side translate a status (Mastodon 4.0+) --
                               * see FS3ENetTranslateStatusReq/Reply below */
    FS3ENETQ_NEWS,           /* fetch trending links (VIEWMODE_News) -- see
                               * FS3ENetNewsReq/Reply below */
    FS3ENETQ_BOOKMARK,       /* toggle bookmark/unbookmark on a status -- same shape
                               * as FS3ENETQ_FAVORITE, see FS3ENetBookmarkReq/Reply below */
    FS3ENETQ_BOOKMARKS_LOCAL, /* list a page of the on-disk bookmarks cache (offline,
                               * no server round trip) -- see FS3ENetBookmarksLocalReq/
                               * Reply below */
    FS3ENETQ_BOOKMARKS_SYNC, /* backfill the on-disk bookmarks cache from the server's
                               * own first page -- see FS3ENetBookmarksSyncReq/Reply below */
    FS3ENETQ_VOTE_POLL,      /* cast a vote on an open poll -- see FS3ENetVotePollReq/Reply
                               * below */
    FS3ENETQ_DOMAIN_BLOCKS,  /* list the connected account's own blocked servers (domains)
                               * -- see FS3ENetDomainBlocksReq/Reply below */
    FS3ENETQ_BLOCK,          /* block an account, from the User menu's "Block user"
                               * item -- see FS3ENetBlockReq/Reply below */
    FS3ENETQ_UNBLOCK,        /* lift a block on an account, from the profile header's
                               * "Unblock" button -- see FS3ENetUnblockReq/Reply below */
    FS3ENETQ_DOMAIN_BLOCK_STATE,  /* check whether the connected account blocks one
                               * domain -- see FS3ENetDomainBlockStateReq/Reply below */
    FS3ENETQ_DOMAIN_BLOCK_TOGGLE, /* block or unblock one domain, from the instance
                               * header's "Block server"/"Unblock server" button --
                               * see FS3ENetDomainBlockToggleReq/Reply below */
    FS3ENETQ_FETCH_PROGRESS /* net-process-originated ONLY -- never sent by the GUI.
                              * A one-way PutMsg() of an FS3ENetFetchProgress block to
                              * app->netReplyPort while a chunked FS3ENETQ_FETCH_IMAGE
                              * download is still in flight; no reply is expected back,
                              * the GUI just frees it like any other FS3ENetMessage off
                              * that port. See FS3ENetFetchProgress below. */
};

/* Result codes returned in FS3ENetMessage.fs3em_Result on reply. */
enum FS3ENetResult
{
    FS3ENETR_OK = 0,
    FS3ENETR_NETWORK_ERROR,
    FS3ENETR_HTTP_ERROR,
    FS3ENETR_AUTH_ERROR,
    FS3ENETR_PARSE_ERROR,
    FS3ENETR_AUTH_REQUIRED /* Server answered but refused an anonymous request
                             * outright ({"error":"This method requires an
                             * authenticated user"} -- Mastodon's "timeline
                             * preview" admin setting turned off, seen on some
                             * instances even for nominally-public endpoints
                             * like timelines/public). Distinct from
                             * FS3ENETR_AUTH_ERROR (a REJECTED, previously-
                             * valid token) -- this is a server policy that
                             * blocks anonymous access outright, not a bad
                             * token; see FS3EMastodon_GetTimeline's
                             * outAuthRequired param. */
};

/*
 * Generic request/reply envelope.
 *
 * fs3em_Msg.mn_ReplyPort is set by the caller before PutMsg(); the network
 * process PutMsg()s the same structure back to that port once done.
 *
 * fs3em_Data/fs3em_DataLen describe a request- or reply-specific payload
 * allocated with AllocVec() by whichever side produces it. Ownership passes
 * to the receiver, which must FreeVec() it.
 */
typedef struct FS3ENetMessage
{
    struct Message fs3em_Msg;
    ULONG           fs3em_Type;    /* enum FS3ENetRequestType */
    ULONG           fs3em_Result;  /* enum FS3ENetResult, set on reply */
    APTR            fs3em_Data;
    ULONG           fs3em_DataLen;
    /* Server's own clock (Unix epoch seconds) as of the most recent
     * raw-BIO HTTP exchange (see FS3EHttp_GetLastServerEpoch() in
     * fs3enet_http.h) at the moment this reply was sent -- 0 if none has
     * been observed yet this session. NOT necessarily from the HTTP
     * exchange THIS reply's own request made (FS3EHttp_Get/Post's
     * OSSL_HTTP_transfer() path never exposes it at all, and chunked
     * FETCH_IMAGE downloads interleave with other requests) -- just "the
     * freshest server time seen recently," which is all a caller correcting
     * a wrong/missing local RTC needs. See TootTimeline's
     * TTIMELINE_ServerClockOffset and ttl_format_poll_remaining's use of
     * it. Left 0 (MEMF_CLEAR'd) on every request; only ever set by the
     * network process on reply. */
    LONG            fs3em_ServerEpoch;
} FS3ENetMessage;

/*
 * All request and reply structs below use char * string fields instead of
 * fixed-size arrays.  Each struct is allocated as a single flat block:
 *
 *   [struct header] + [string data packed contiguously]
 *
 * The char * fields point into the same block, so one FreeVec() on the
 * fs3em_Data pointer frees the struct and all its strings.  fs3em_DataLen
 * is set to the total block size (not sizeof(struct)).
 *
 * Use the _Alloc() helpers below to build request blocks; the network
 * process builds reply blocks internally.
 */

/*
 * FS3ENETQ_LOGIN_START — registers FriendSh3ep as an OAuth app on
 * fs3enl_ApiBaseUrl (see ARCHITECTURE.md section 4.4).
 *
 * On FS3ENETR_OK, fs3em_Data is replaced with an FS3ENetLoginStartReply; the
 * GUI must show fs3enl_AuthorizeUrl to the user and keep
 * fs3enl_ClientId/fs3enl_ClientSecret for FS3ENETQ_LOGIN_FINISH.
 * On error, fs3em_Data still points at the original request block — the GUI
 * must FreeVec it.
 */
typedef struct FS3ENetLoginStartReq
{
    char *fs3enl_ApiBaseUrl;
} FS3ENetLoginStartReq;

/* Allocates a flat request block for LOGIN_START. FreeVec() when done. */
FS3ENetLoginStartReq *FS3ENetLoginStartReq_Alloc(const char *apiBaseUrl);

typedef struct FS3ENetLoginStartReply
{
    char *fs3enl_ClientId;
    char *fs3enl_ClientSecret;
    char *fs3enl_AuthorizeUrl;
} FS3ENetLoginStartReply;

/*
 * FS3ENETQ_LOGIN_FINISH — exchanges the OOB code for an access token and
 * verifies it (brutaldon's mastodon.log_in + verify_credentials).
 *
 * On FS3ENETR_OK, fs3em_Data is replaced with an FS3ENetLoginFinishReply; the
 * GUI must persist fs3enl_AccessToken (and the api base URL) for later
 * FS3ENETQ_TIMELINE/FS3ENETQ_POST_STATUS requests.
 * On error, fs3em_Data still points at the original request block.
 *
 * Note: fs3enl_Account strings point into the same flat reply block.
 * Do NOT call FS3EMastodonAccount_Free() on fs3enl_Account; FreeVec the
 * whole block instead.
 */
typedef struct FS3ENetLoginFinishReq
{
    char *fs3enl_ApiBaseUrl;
    char *fs3enl_ClientId;
    char *fs3enl_ClientSecret;
    char *fs3enl_Code;
} FS3ENetLoginFinishReq;

/* Allocates a flat request block for LOGIN_FINISH. FreeVec() when done. */
FS3ENetLoginFinishReq *FS3ENetLoginFinishReq_Alloc(const char *apiBaseUrl,
    const char *clientId, const char *clientSecret, const char *code);

typedef struct FS3ENetLoginFinishReply
{
    char               *fs3enl_AccessToken;
    FS3EMastodonAccount  fs3enl_Account;
} FS3ENetLoginFinishReply;

/*
 * FS3ENETQ_VERIFY_ACCOUNT — re-runs verify_credentials for an access token
 * the GUI already has (no OAuth exchange, unlike LOGIN_FINISH). Used to
 * backfill account fields added after a user's account.dat was last saved
 * (e.g. fma_Id, needed by VIEWMODE_User's accounts/{id}/statuses fetch) --
 * see FS3EApp_LoadAccount() in friendsh3ep.c.
 *
 * On FS3ENETR_OK, fs3em_Data is replaced with an FS3ENetVerifyAccountReply.
 * On error, fs3em_Data still points at the original request block.
 */
typedef struct FS3ENetVerifyAccountReq
{
    char *fs3eva_ApiBaseUrl;
    char *fs3eva_AccessToken;
} FS3ENetVerifyAccountReq;

/* Allocates a flat request block for VERIFY_ACCOUNT. FreeVec() when done. */
FS3ENetVerifyAccountReq *FS3ENetVerifyAccountReq_Alloc(const char *apiBaseUrl,
    const char *accessToken);

typedef struct FS3ENetVerifyAccountReply
{
    FS3EMastodonAccount fs3eva_Account;
} FS3ENetVerifyAccountReply;

/*
 * FS3ENETQ_INSTANCE_INFO — fetches the server's per-toot character limit
 * (see FS3EMastodon_GetInstanceInfo). No access token needed. Meant to be
 * sent once per account (right after login/load, see
 * FS3EApp_SetAccount() in friendsh3ep.c) rather than per-compose, since
 * an instance's limit essentially never changes mid-session.
 *
 * On FS3ENETR_OK, fs3em_Data is replaced with an FS3ENetInstanceInfoReply.
 * fs3eii_Known says whether fs3eii_MaxChars is a real value the server
 * confirmed, or just FS3EMastodon_GetInstanceInfo's own best-guess
 * fallback because neither instance endpoint answered -- the GUI must NOT
 * present that fallback as if it were a real limit (see
 * FS3ETootView_UpdateCharCount in fs3etootview.c, which shows "Max: -"
 * rather than a possibly-wrong number until fs3eii_Known is TRUE for the
 * currently active account).
 * On error, fs3em_Data still points at the original request block.
 */
typedef struct FS3ENetInstanceInfoReq
{
    char *fs3eii_ApiBaseUrl;
} FS3ENetInstanceInfoReq;

/* Allocates a flat request block for INSTANCE_INFO. FreeVec() when done. */
FS3ENetInstanceInfoReq *FS3ENetInstanceInfoReq_Alloc(const char *apiBaseUrl);

typedef struct FS3ENetInstanceInfoReply
{
    ULONG fs3eii_MaxChars;
    BOOL  fs3eii_Known;
    BOOL  fs3eii_TranslationEnabled; /* configuration.translation.enabled (v2, Mastodon
                                       * 4.0+) -- whether the CONNECTED account's own
                                       * server offers server-side toot translation.
                                       * Read only when fs3eii_TranslationKnown is TRUE,
                                       * same "unknown isn't a negative" rule as
                                       * fs3eii_Known/fs3eii_MaxChars above. */
    BOOL  fs3eii_TranslationKnown;
} FS3ENetInstanceInfoReply;

/*
 * FS3ENETQ_INSTANCE_DETAILS — a full "about this server" profile for
 * fs3eid_ApiBaseUrl (see FS3EMastodon_GetInstanceDetails), not just the
 * compose-time character limit FS3ENETQ_INSTANCE_INFO above covers.
 * Deliberately a separate, heavier request type: fs3eid_ApiBaseUrl here
 * need not be the connected account's own instance -- see
 * FS3EApp_SearchInstance() (fs3erequests.c), which looks up ANY domain the
 * user types, logged in there or not. No access token needed; every field
 * comes from Mastodon's public instance endpoints.
 *
 * On FS3ENETR_OK, fs3em_Data is replaced with a flat
 * FS3ENetInstanceDetailsReply. On FS3ENETR_HTTP_ERROR (server unreachable
 * outright), fs3em_Data still points at the original request block.
 */
typedef struct FS3ENetInstanceDetailsReq
{
    char *fs3eid_ApiBaseUrl;
} FS3ENetInstanceDetailsReq;

/* Allocates a flat request block for INSTANCE_DETAILS. FreeVec() when done. */
FS3ENetInstanceDetailsReq *FS3ENetInstanceDetailsReq_Alloc(const char *apiBaseUrl);

/* Mirrors FS3E_MASTODON_MAX_RULES in fs3enet_mastodon.h -- both MUST stay
 * numerically equal, see that define's own comment for why this is a
 * mirrored plain value rather than a shared include. */
#define FS3ENET_MAX_INSTANCE_RULES 16

typedef struct FS3ENetInstanceDetailsReply
{
    char *fs3eid_Domain;         /* "" if the server didn't echo its own domain --
                                   * caller falls back to whatever it looked up */
    char *fs3eid_Title;
    char *fs3eid_Version;
    char *fs3eid_Description;    /* plain text (v2), or raw HTML on the v1-only
                                   * fallback path -- see FS3EMastodon_GetInstanceDetails */
    char *fs3eid_ContactEmail;
    char *fs3eid_ContactAccount; /* acct string, "" if none */

    ULONG fs3eid_MaxChars;
    BOOL  fs3eid_MaxCharsKnown;
    ULONG fs3eid_MaxMediaAttachments;
    ULONG fs3eid_ImageSizeLimit;  /* bytes, 0 = unknown */
    ULONG fs3eid_VideoSizeLimit;  /* bytes, 0 = unknown */
    ULONG fs3eid_PollMaxOptions;
    ULONG fs3eid_PollMaxExpirationSecs;

    BOOL  fs3eid_TranslationEnabled;
    BOOL  fs3eid_TranslationKnown;

    BOOL  fs3eid_RegistrationsEnabled;
    BOOL  fs3eid_RegistrationsKnown;
    BOOL  fs3eid_ApprovalRequired; /* meaningful only if RegistrationsKnown && Enabled */

    ULONG fs3eid_UserCount;
    BOOL  fs3eid_UserCountKnown;
    ULONG fs3eid_StatusCount;
    BOOL  fs3eid_StatusCountKnown;
    ULONG fs3eid_ActiveMonthUsers;
    BOOL  fs3eid_ActiveMonthUsersKnown;

    ULONG fs3eid_RuleCount; /* <= FS3ENET_MAX_INSTANCE_RULES */
    char *fs3eid_Rules[FS3ENET_MAX_INSTANCE_RULES];
} FS3ENetInstanceDetailsReply;

/* FS3ECache subdirectories (see fs3enet_cache.h) -- user avatars and toot
 * media thumbnails are fetched through the identical pipeline but kept in
 * their own cache subdirectory rather than one flat pile of hash-named
 * files, since they're conceptually distinct sets. Shared here (rather
 * than a private #define in friendsh3ep.c) so any caller building an
 * FS3ENetFetchImageReq -- friendsh3ep.c's avatar/thumbnail fetches,
 * fs3emediaview.c's on-demand full-image fetch -- names the same
 * subdirectory. */
#define FS3E_CACHE_SUBDIR_USERICONS   "usericons"
#define FS3E_CACHE_SUBDIR_THUMBNAILS  "thumbnails"
/* Kept distinct from THUMBNAILS (not reused) so the FS3ENETQ_FETCH_IMAGE
 * reply handler (fs3erequests.c) can tell a card-image reply apart from a
 * real attachment-thumbnail reply by subdir alone, and so a card's image
 * URL can never collide with an actual attachment's cache entry. */
#define FS3E_CACHE_SUBDIR_CARDIMAGES  "cardimages"
/* Toot audio attachments (mp3/wav/ogg) fetched whole -- see
 * fs3emediaview.c's FS3EMediaView_ShowAudioUrl(). Own subdir for the same
 * "never collide with, or get swept up as, a real image thumbnail" reasoning
 * as CARDIMAGES above. Always fetched with keepOriginal=TRUE (there is no
 * RAM:T-then-minify step for audio -- fs3eaudio.c decodes straight from
 * this cached file). */
#define FS3E_CACHE_SUBDIR_AUDIO       "audio"

/*
 * FS3ENETQ_FETCH_IMAGE — fetch a media URL (avatar, attachment thumbnail,
 * custom emoji) and cache it under T:FS3ECache/.  The network process serves
 * from disk cache when the file is already present; it only hits the network
 * on a cache miss.
 *
 * On a cache miss, the network process downloads the file in bounded chunks
 * (see FS3ENetActiveDownload in fs3enet.c) instead of one unbounded blocking
 * fetch, so a large/slow download can't stall other queued requests, and a
 * dead connection times out per-chunk instead of hanging forever. This is
 * purely an internal implementation detail: fs3em_Data/fs3em_Result still
 * arrive exactly once, when the whole file is done or has failed, with the
 * same shape as before chunking existed. If fs3enf_WantProgress is TRUE, the
 * caller ALSO gets zero or more FS3ENETQ_FETCH_PROGRESS pings on
 * app->netReplyPort while the download is in flight -- see
 * FS3ENetFetchProgress below.
 *
 * On FS3ENETR_OK, fs3em_Data is a flat FS3ENetFetchImageReply block whose
 * fs3enf_LocalPath is a NUL-terminated AmigaOS path the GUI can open with
 * NewDTObject() (no file extension; datatype detects JPEG/PNG from magic).
 * On error, fs3em_Data still points at the original request block.
 *
 * The URL need not carry an Authorization header: Mastodon CDN URLs are
 * pre-signed and publicly accessible regardless of auth state.
 */
typedef struct FS3ENetFetchImageReq
{
    char *fs3enf_Url;
    char *fs3enf_Key;     /* caller key echoed in reply; @acct for avatars, the URL itself for media */
    char *fs3enf_Subdir;  /* FS3ECache_Lookup/Store subdir, e.g. "usericons"/"thumbnails"; "" = cache root */
    BOOL  fs3enf_KeepOriginal; /* FALSE = download to FS3ECACHE_RAM_TEMP_DIR instead of the
                                * persistent cache dir (see "Keep big user icons/thumbnails" in
                                * Settings) -- ignored on a cache hit against an already-persisted
                                * original from an earlier TRUE request. */
    BOOL  fs3enf_WantProgress; /* opt in to FS3ENETQ_FETCH_PROGRESS pings for this download.
                                 * FALSE for routine avatar/thumbnail fetches (no progress UI
                                 * for those today) -- TRUE for the media viewer's on-demand
                                 * full-image fetch, the case this was actually added for. */
    char *fs3enf_ExactLocalPath; /* "" (the common case, built by FS3ENetFetchImageReq_Alloc)
                                * = normal cache-computed path, everything above applies as
                                * documented. Non-empty (built by
                                * FS3ENetFetchImageReq_AllocDownload) = the caller already
                                * picked this exact final AmigaOS path (a file-save requester
                                * result, see fs3emanageurl.c's archive download) -- not a
                                * cache entry at all: fs3enf_Subdir/fs3enf_KeepOriginal are
                                * ignored, there is no lookup/RAM:T reuse, and the reply's
                                * fs3enf_IsTemp is always FALSE. Always downloads fresh,
                                * overwriting whatever's already at that path, same as any
                                * other "Save As" would. See FS3ENet_HandleFetchImage(). */
} FS3ENetFetchImageReq;

/* Allocates a flat request block for FETCH_IMAGE. FreeVec() when done.
 * key is echoed back in the reply so the caller knows which entry to update. */
FS3ENetFetchImageReq *FS3ENetFetchImageReq_Alloc(const char *url, const char *key,
                                                   const char *subdir, BOOL keepOriginal,
                                                   BOOL wantProgress);

/* Allocates a flat request block for FETCH_IMAGE in "exact path" mode --
 * see fs3enf_ExactLocalPath's doc comment above. key is set to url itself
 * (the natural choice: there is no separate caller-side identity for a
 * one-off download the way an avatar/media URL has one). Always
 * KeepOriginal-like (never RAM:T) and always WantProgress (an archive
 * download is exactly the case FS3ENETQ_FETCH_PROGRESS pings are worth
 * having). FreeVec() when done, same as FS3ENetFetchImageReq_Alloc(). */
FS3ENetFetchImageReq *FS3ENetFetchImageReq_AllocDownload(const char *url,
                                                           const char *exactLocalPath);

typedef struct FS3ENetFetchImageReply
{
    char *fs3enf_LocalPath;  /* e.g. "PROGDIR:.cache/usericons/1a2b3c4d" or "RAM:T/1a2b3c4d" */
    char *fs3enf_Key;        /* echoed from request */
    char *fs3enf_Subdir;     /* echoed from request -- lets the GUI dispatch avatar vs media handling */
    BOOL  fs3enf_IsTemp;     /* TRUE = fs3enf_LocalPath is a RAM:T download the caller must
                               * delete once it's done with it (see FS3EThumb_Request's
                               * deleteSrcAfter) -- FALSE if it's already permanently cached. */
    char *fs3enf_CachePath;  /* deterministic path this URL would live at under fs3enf_Subdir
                               * if kept, computed regardless of fs3enf_IsTemp (see
                               * FS3ECache_ComputePath) -- pass as FS3EThumb_Request's
                               * cacheKeyPath so the resized thumbnail always gets a name
                               * stable across runs, even when fs3enf_LocalPath itself is
                               * a transient RAM:T path. */
} FS3ENetFetchImageReply;

/*
 * FS3ENETQ_FETCH_PROGRESS — see fs3enf_WantProgress above. Sent unsolicited
 * by the network process, zero or more times, while a chunked FETCH_IMAGE
 * download is in flight; correlate with the original request/final reply via
 * fs3efp_Key (the same caller key FS3ENetFetchImageReq/Reply already carry --
 * see e.g. fs3emediaview.c's mv->pendingUrl match against reply->fs3enf_Key).
 * fs3efp_TotalBytes is 0 until the first chunk's response tells us the real
 * size (Content-Range's "/TOTAL" suffix) -- some servers never do (they
 * ignore Range entirely), in which case it stays 0 for the whole download and
 * the caller can only show bytes-so-far, not a percentage.
 */
typedef struct FS3ENetFetchProgress
{
    char  *fs3efp_Key;
    ULONG  fs3efp_BytesSoFar;
    ULONG  fs3efp_TotalBytes; /* 0 = unknown */
} FS3ENetFetchProgress;

/*
 * Start the network process. cacheDir and maxCacheSizeMB are passed straight
 * to FS3ECache_Init() inside the new process; pass NULL for cacheDir to use
 * FS3ECACHE_DEFAULT_DIR, 0 for maxCacheSizeMB for an unbounded cache.
 * Returns the request MsgPort, or NULL on failure.
 */
struct MsgPort *FS3ENet_Start(const char *cacheDir, ULONG maxCacheSizeMB);

/*
 * Ask the network process to shut down and wait for it to exit.
 * requestPort is the port returned by FS3ENet_Start(); replyPort is a
 * temporary port created by the caller to receive the shutdown reply.
 */
void FS3ENet_Stop(struct MsgPort *requestPort, struct MsgPort *replyPort);

/*
 * Ask the network process to flush (delete every file in) its disk cache
 * and wait for the reply. requestPort/replyPort as FS3ENet_Stop().
 * Returns TRUE on FS3ENETR_OK, FALSE otherwise (including requestPort==NULL).
 */
BOOL FS3ENet_FlushCache(struct MsgPort *requestPort, struct MsgPort *replyPort);

/*
 * FS3ENETQ_SET_CACHE_DIR — live-updates the disk cache directory and max
 * size of an already-running network process (e.g. SettingsView's cache
 * path GetFile gadget, which used to silently do nothing until next
 * launch -- FS3ENet_Start()'s cacheDir/maxCacheSizeMB were only ever read
 * once, at process startup). Re-runs FS3ECache_Init() with the new values;
 * already-cached files under the old path are left behind, not moved.
 */
typedef struct FS3ENetSetCacheDirReq
{
    char  *fs3escd_CacheDir;
    ULONG  fs3escd_MaxCacheSizeMB;
} FS3ENetSetCacheDirReq;

/* Allocates a flat request block for SET_CACHE_DIR. FreeVec() when done. */
FS3ENetSetCacheDirReq *FS3ENetSetCacheDirReq_Alloc(const char *cacheDir, ULONG maxCacheSizeMB);

/*
 * Ask the network process to switch its disk cache to cacheDir/maxCacheSizeMB
 * and wait for the reply. requestPort/replyPort as FS3ENet_Stop().
 * Returns TRUE on FS3ENETR_OK (new dir created and now in use), FALSE
 * otherwise (including requestPort==NULL or the new dir being uncreatable,
 * in which case the process keeps using whatever cache dir it had before).
 */
BOOL FS3ENet_SetCacheDir(struct MsgPort *requestPort, struct MsgPort *replyPort,
    const char *cacheDir, ULONG maxCacheSizeMB);

/* Which direction a FS3ENETQ_TIMELINE request pages in -- echoed back into
 * FS3ENetTimelineReply so the GUI knows how to splice the results into its
 * post list (prepend at the top vs. append at the bottom) and which
 * in-flight guard to clear, without having to remember what it asked for. */
enum FS3ENetPageDirection
{
    FS3ENETPAGE_INITIAL = 0,  /* first page for this channel; fs3et_MaxId/MinId both "" */
    FS3ENETPAGE_OLDER,        /* fs3et_MaxId set: statuses strictly older than it */
    FS3ENETPAGE_NEWER         /* fs3et_MinId set: statuses strictly newer than it */
};

/* Which JSON shape fs3et_Timeline's endpoint returns -- lets
 * FS3ENETQ_TIMELINE be reused for endpoints that don't hand back a bare
 * status array, without forking the per-status field-extraction parser
 * (content, media_attachments, poll, counts, ...) that ARRAY already
 * shares with every timeline/profile fetch. Echoed back into
 * FS3ENetTimelineReply the same way fs3et_ViewModeBit/PageDirection are,
 * so the GUI reply handler knows which shape it got without depending on
 * mutable app state that could have changed by the time the reply lands. */
enum FS3ENetTimelineShape
{
    FS3ENET_TLSHAPE_ARRAY = 0,       /* bare Status[] -- every timeline/profile-statuses endpoint */
    FS3ENET_TLSHAPE_SINGLE,          /* GET .../statuses/:id -- one Status object, wrapped as a 1-elem array */
    FS3ENET_TLSHAPE_CONTEXT_DESCENDANTS, /* GET .../statuses/:id/context -- {ancestors,descendants}; only
                                          * descendants (the replies) is unwrapped and used, ancestors
                                          * discarded */
    FS3ENET_TLSHAPE_SEARCH_STATUSES, /* GET /api/v2/search?type=statuses&q=... (note: v2, not v1) --
                                          * {accounts,statuses,hashtags}; only statuses is unwrapped and
                                          * used. Word and hashtag search both use this same shape/request
                                          * (see fs3et_SearchQuery below) -- Mastodon's own search treats
                                          * a leading '#' in q as a hashtag match, so there's no need for
                                          * a separate hashtag-timeline request type. */
    FS3ENET_TLSHAPE_SINGLE_REFRESH,  /* GET .../statuses/:id, same one-Status-object wire shape as
                                          * FS3ENET_TLSHAPE_SINGLE, but a different GUI-side meaning: an
                                          * F5-triggered refresh of an already-displayed toot rather than
                                          * a toot being newly inserted. fs3et_MinId is repurposed (see
                                          * FS3ENetTimelineReq's doc comment) to carry the TTLPost.postId
                                          * to patch, echoed back via FS3ENetTimelineReply.fs3et_RefreshPostId
                                          * -- see FS3EApp_RefreshVisibleToots(). */
    FS3ENET_TLSHAPE_SEARCH_ACCOUNTS, /* GET /api/v2/search?type=accounts&q=... -- same
                                          * {accounts,statuses,hashtags} wrapper as SEARCH_STATUSES,
                                          * just unwrapping "accounts" instead of "statuses". Not used
                                          * by FS3ENetTimelineReq/FS3ENETQ_TIMELINE at all -- this value
                                          * is only ever passed to FS3EMastodon_GetTimeline() directly
                                          * by FS3ENET_HandleAccountsList() (FS3ENETQ_ACCOUNTS_LIST),
                                          * which has its own request/reply pair and its own
                                          * FS3ENetAccountsListKind discriminator. */
    FS3ENET_TLSHAPE_CONTEXT_ANCESTORS /* Same GET .../statuses/:id/context endpoint and JSON shape as
                                          * CONTEXT_DESCENDANTS above, just unwrapping "ancestors" (the
                                          * chain of toots this one replied to, root first) instead --
                                          * see FS3EApp_OpenDiscussion's includeAncestors param.
                                          * Deliberately appended at the END of this enum, not grouped
                                          * next to CONTEXT_DESCENDANTS -- fs3enet_mastodon.c can't
                                          * include this header (see its own mirrored #define list's
                                          * comment) and keeps its own plain-int copies of these values
                                          * in sync by NUMBER, so inserting a new entry anywhere but the
                                          * end would silently renumber every later shape out of sync
                                          * between the two files. */
};

/*
 * FS3ENETQ_TIMELINE — fetch one page of statuses for a timeline.
 *
 * fs3et_ViewModeBit identifies the UI channel (VIEWMODE_* value from
 * friendsh3ep.h); it is echoed unchanged into FS3ENetTimelineReply so the
 * GUI can route replies back to the right TootTimeline channel.
 * fs3et_AccountGeneration is an opaque caller-defined token (FriendSh3ep
 * stamps its App.accountGeneration, bumped on every login/account switch)
 * echoed back the same way -- lets the caller tell a reply for the account
 * that was active when the request was sent apart from one for whatever
 * account is active *now*, since two FS3ENETQ_TIMELINE requests for the
 * same fs3et_ViewModeBit sent under different accounts (e.g. one still in
 * flight when the user switches accounts) are otherwise indistinguishable
 * once the reply comes back -- this process treats it as opaque and never
 * inspects it.
 * fs3et_AccessToken may be "" for public timelines.
 * fs3et_MaxId/fs3et_MinId may be NULL/empty; at most one should be set (see
 * fs3et_PageDirection) -- fs3et_MaxId asks for statuses strictly older than
 * that status id (contiguous with what the GUI already has at the bottom
 * of its list), fs3et_MinId strictly newer (contiguous at the top). Both
 * empty means "the newest page" (FS3ENETPAGE_INITIAL).
 *
 * On FS3ENETR_OK, fs3em_Data is replaced with a flat FS3ENetTimelineReply
 * block; fs3em_Data on error still points at the original request block.
 *
 * fs3et_SearchQuery is only meaningful for FS3ENET_TLSHAPE_SEARCH_STATUSES:
 * the raw (NOT URL-encoded) search text -- FS3ENet_HandleTimeline encodes
 * it itself and folds it onto fs3et_Timeline as "&q=...". "" for every
 * other shape.
 *
 * fs3et_MinId is repurposed for FS3ENET_TLSHAPE_SINGLE_REFRESH: its normal
 * "page strictly newer than this id" pagination meaning doesn't apply to a
 * single-status fetch, so it instead carries the TTLPost.postId the GUI
 * should patch once the reply lands (see FS3ENetTimelineReply.fs3et_RefreshPostId) --
 * this can differ from the refetched status's own id in the notifications
 * view, where TTLPost.postId is the notification's own id, not the embedded
 * status's (see TTLPostSetup.notifStatusId in fs3etoottimeline.h).
 */
typedef struct FS3ENetTimelineReq {
    ULONG  fs3et_ViewModeBit;    /* echoed in reply */
    ULONG  fs3et_PageDirection;  /* FS3ENetPageDirection; echoed in reply */
    ULONG  fs3et_AccountGeneration; /* opaque caller token; echoed in reply */
    ULONG  fs3et_ResponseShape;  /* FS3ENetTimelineShape; echoed in reply */
    char  *fs3et_ApiBaseUrl;
    char  *fs3et_AccessToken;    /* "" = no auth (public timelines) */
    char  *fs3et_Timeline;       /* "home", "public", "public?local=true", … */
    char  *fs3et_MaxId;          /* "" = no lower bound */
    char  *fs3et_MinId;          /* "" = no upper bound */
    char  *fs3et_SearchQuery;    /* raw (unencoded) search text; "" unless
                                   * fs3et_ResponseShape is SEARCH_STATUSES */
} FS3ENetTimelineReq;

FS3ENetTimelineReq *FS3ENetTimelineReq_Alloc(ULONG viewModeBit,
    ULONG pageDirection, ULONG accountGeneration, ULONG responseShape,
    const char *apiBaseUrl, const char *accessToken, const char *timeline,
    const char *maxId, const char *minId, const char *searchQuery);

/* Largest attachment FS3ENETQ_UPLOAD_MEDIA will read/upload, in bytes --
 * conservative given the whole file is buffered in RAM twice over (once as
 * read off disk, once inside the multipart body FS3EMastodon_UploadMedia
 * builds around it) with no streaming, on hardware that may only have a
 * few MB of Fast RAM to spare. The GUI checks this BEFORE ever sending the
 * request (see fs3etootview.c/friendsh3ep.c's GID_TOOT_SEND_BUTTON), so a
 * user sees the rejection immediately instead of after a slow upload
 * attempt; FS3ENet_HandleUploadMedia() re-checks it too, defensively, in
 * case the file grew between selection and send. */
#define FS3ENET_UPLOAD_MAX_BYTES (16UL * 1024UL * 1024UL) /* 16 MiB */

/* Max media_attachments entries kept per status (Mastodon itself caps
 * normal posts at 4 attachments, so this never truncates in practice).
  KRB says; yes but could evolve, and does not cost : ->8
 */
#define FS3ENET_MAX_MEDIA 8

/* Max poll options kept per status. Vanilla Mastodon's own default cap is
 * 4, but some instances raise it -- 8 leaves headroom without a real cost
 * (just a handful of extra pointer-sized slots per FS3ENetStatus). */
#define FS3ENET_MAX_POLL_OPTIONS 8

/* Mastodon media_attachments[].type, mapped from the JSON string. Lets
 * the GUI tell an audio attachment apart from an image *before* ever
 * downloading anything for it -- audio has no thumbnail to fetch, and
 * routing its (fallback) full-file URL into the image decoder is exactly
 * what used to make MP3s show up as failed image loads. */
enum FS3ENetMediaKind
{
    FS3ENET_MEDIAKIND_IMAGE = 0,
    FS3ENET_MEDIAKIND_VIDEO,
    FS3ENET_MEDIAKIND_GIFV,
    FS3ENET_MEDIAKIND_AUDIO,
    FS3ENET_MEDIAKIND_UNKNOWN
};

/* Single status entry inside a FS3ENetTimelineReply.
 * All char * fields point into the same flat block — one FreeVec() on
 * the enclosing FS3ENetTimelineReply frees everything. */
typedef struct FS3ENetStatus {
    char *fmas_DisplayName;  /* original author display_name (UTF-8) */
    char *fmas_Acct;         /* original author @user@instance handle */
    char *fmas_Content;      /* HTML-stripped plain text */
    char *fmas_CreatedAt;    /* ISO 8601 timestamp string */
    char *fmas_AvatarURL;    /* original author CDN avatar URL */
    char *fmas_Id;           /* status id string (for pagination) -- for a genuine
                               * reblog-unwrapped entry this is the REBLOG WRAPPER's
                               * own id (item's "id"), needed for timeline cursor
                               * correctness, NOT the id to interact with -- see
                               * fmas_TargetId for that. Equal to fmas_TargetId
                               * whenever this isn't a reblog wrapper. */
    char *fmas_TargetId;     /* the id every interaction (reply/boost/fave/modify/
                               * delete/thread) should actually target -- src's own
                               * "id", i.e. the ORIGINAL status' id for a reblog
                               * wrapper (Mastodon's interaction endpoints operate on
                               * real content, not a reblog's own wrapper row). See
                               * TTLPost.targetId for where this ends up in TootTimeline. */
    char *fmas_BoostBy;      /* booster display_name, "" if not a reblog */
    char *fmas_BoostByAcct;  /* booster @user@instance handle, "" if not a reblog --
                               * what a click on the "X boosted" line actually needs
                               * to look up their profile; the display name alone
                               * isn't a valid /api/v1/accounts/lookup query. */
    char *fmas_Language;     /* status.language -- ISO 639 code, "" if the server left
                               * it null (undetected/unset). Belongs to src, same as
                               * fmas_Content (a reblog's language is the boosted
                               * status's own). Drives TootTimeline's "Translate"
                               * hot-spot -- see TTLPostSetup.canTranslate/
                               * FS3EApp_MapStatusToPostSetup. */

    /* media_attachments[].preview_url (falling back to .url if no
     * preview_url) for up to FS3ENET_MAX_MEDIA attachments; entries
     * [fmas_MediaCount..FS3ENET_MAX_MEDIA) are NULL. For an audio
     * attachment that has separate cover art, preview_url is that cover
     * IMAGE, not the audio -- see fmas_MediaAudioUrls below for the
     * attachment's actual playable file in that case. */
    char  *fmas_MediaUrls[FS3ENET_MAX_MEDIA];
    /* media_attachments[].url, unconditionally (never the preview_url
     * fallback fmas_MediaUrls above uses) -- the attachment's real,
     * original file. Only meaningfully different from fmas_MediaUrls[i]
     * when that slot had a distinct preview_url (cover art on an audio
     * attachment is the case that matters today: fmas_MediaUrls[i] is
     * then the cover image and this is the actual mp3/ogg/wav to play).
     * For every other kind (image/video/gifv, or audio with no cover),
     * this is equal to (or the same value fmas_MediaUrls[i] already
     * fell back to). Same NULL-past-fmas_MediaCount convention. */
    char  *fmas_MediaAudioUrls[FS3ENET_MAX_MEDIA];
    /* media_attachments[].type for the same slots -- enum FS3ENetMediaKind. */
    ULONG  fmas_MediaKind[FS3ENET_MAX_MEDIA];
    /* media_attachments[].id for the same slots -- needed to resend as
     * media_ids[] on a PUT edit of this status, since Mastodon treats that
     * field as a full replace-list: omit it and existing attachments get
     * stripped even if the edit never touched them. */
    char  *fmas_MediaIds[FS3ENET_MAX_MEDIA];
    ULONG  fmas_MediaCount;

    /* Action-bar counts/state -- for reblogs these belong to the boosted
     * status (Mastodon reports them there, not on the outer reblog
     * wrapper), same as fmas_Content/fmas_MediaUrls above. */
    ULONG  fmas_RepliesCount;
    ULONG  fmas_ReblogsCount;
    ULONG  fmas_FavouritesCount;
    BOOL   fmas_Favourited;   /* connected user already favourited this status */
    BOOL   fmas_Reblogged;    /* connected user already boosted this status */
    BOOL   fmas_Bookmarked;   /* connected user already bookmarked this status --
                               * no fmas_BookmarksCount: Mastodon never exposes a
                               * public bookmark count, private-to-the-bookmarker
                               * same as the button itself. */

    /* Mastodon's `sensitive` flag -- applies to the whole status incl.
     * every attachment, not per-attachment (see FS3ENetPostStatusReq.
     * fs3ep_Sensitive's comment for the posting-side equivalent). Drives
     * TootTimeline's blur/reveal toggle -- see TTLPost.sensitive and
     * TTLPost.contentRevealed. */
    BOOL   fmas_Sensitive;

    /* TRUE if this status is itself a reply (src's own "in_reply_to_id" is
     * non-null) -- doesn't need the actual parent id, just whether one
     * exists: fetching GET .../statuses/:id/context with THIS status' own
     * id already returns the full ancestor chain regardless (see
     * FS3ENET_TLSHAPE_CONTEXT_ANCESTORS). Drives whether TootTimeline shows
     * a "Follow discussion up" affordance alongside the existing "...down"
     * one -- see TTLPost.isReply. */
    BOOL   fmas_IsReply;

    /* Whether the connected user is currently allowed to Quote this status
     * -- from quote_approval.current_user (Mastodon 4.5+, absent on older
     * servers): "automatic"/"manual" both mean TRUE (manual just means the
     * quote goes to the author for approval first, still worth offering),
     * "denied"/"unknown"/missing mean FALSE. See
     * FS3EMastodon_PostStatus's quotedStatusId param for the actual quote
     * submission, and TTLPostSetup.quotable for where this ends up in
     * TootTimeline. */
    BOOL   fmas_Quotable;

    /* Embedded quote (Mastodon 4.4+, status.quote) -- the status this one
     * is itself quoting, if any. Only populated when quote.state=="accepted"
     * (the only state guaranteed to carry the quoted status' own content);
     * pending/rejected/revoked/deleted/etc. leave fmas_HasQuote FALSE, same
     * "nothing to render" treatment as a card-less status -- see
     * FS3ENet_FillStatusFields. Deliberately a flat handful of fields (not
     * a nested struct/full second FS3ENetStatus) since TootTimeline only
     * ever shows a minimal avatar+name+body+timestamp block for a quoted
     * status, never its own action bar/media/poll/card -- see
     * TTLPost.quoteBody's comment for why (bounded recursion: a quote of a
     * quote does not itself show a nested quote block). */
    BOOL   fmas_HasQuote;
    char  *fmas_QuoteId;           /* quoted status' own id, for opening its thread */
    char  *fmas_QuoteAuthorName;
    char  *fmas_QuoteAuthorAcct;
    char  *fmas_QuoteAvatarURL;
    char  *fmas_QuoteContent;      /* HTML-stripped plain text */
    char  *fmas_QuoteCreatedAt;

    /* Poll ("survey"). Mutually exclusive with media_attachments above --
     * Mastodon itself disallows a status having both -- so the GUI treats
     * them as alternatives, not something that can coexist in one post.
     * fmas_PollOptionCount==0 means no poll on this status. An option's
     * votes_count comes back JSON null (not present) from the server
     * until the poll is closed or the connected user has voted -- packed
     * as 0 either way, matching what there's actually something to show. */
    char  *fmas_PollOptionTitles[FS3ENET_MAX_POLL_OPTIONS];
    ULONG  fmas_PollOptionVotes[FS3ENET_MAX_POLL_OPTIONS];
    ULONG  fmas_PollOptionCount;
    ULONG  fmas_PollVotesCount;   /* total votes across all options -- percentage denominator */
    BOOL   fmas_PollExpired;      /* TRUE = closed, results are final */
    BOOL   fmas_PollMultiple;     /* TRUE = multiple-choice poll (not used yet, carried for later) */
    /* The poll's OWN id -- distinct from the status id, needed to POST
     * /api/v1/polls/:id/votes (submitting a vote is future work, see
     * TootTimeline/fs3etoottimeline_posts.c's TTL_HOT_POLL_VOTE build; this
     * is carried now so it's already in place when that lands). "" when
     * fmas_PollOptionCount==0. */
    char  *fmas_PollId;
    /* ISO8601 "YYYY-MM-DDTHH:MM:SS.sssZ", always UTC, same shape as
     * fmas_CreatedAt -- "" if the server sent JSON null (an anonymous
     * client-created poll with no expiry, or an ancient server predating
     * this field). Used to compute the "X hours/days left" the GUI shows
     * on an open poll in place of "Poll closed". */
    char  *fmas_PollExpiresAt;
    /* TRUE if the CONNECTED user has already cast a vote on this poll
     * (Mastodon's own "voted" field -- only meaningful when authenticated;
     * FALSE for an anonymous/unauthenticated fetch, same as every other
     * *_Favourited/Reblogged/Bookmarked-style "did I already..." flag).
     * Drives whether the GUI shows the vote-picker (radio buttons) or the
     * result bars with no vote hot-spots. */
    BOOL   fmas_PollVoted;

    /* Link preview "card" -- server-generated (Mastodon itself fetches the
     * linked page's OpenGraph tags when the toot is posted and caches the
     * result), never fetched or parsed by this client. Independent of
     * fmas_MediaCount/poll above -- NOT mutually exclusive with either,
     * since a card comes from a URL in the text, not from what the user
     * attached. fmas_HasCard==FALSE means every other fmas_Card* field
     * below is "" and meaningless. Belongs to src same as content/media/
     * poll (a reblog's card is the boosted status's own, never the outer
     * wrapper's). */
    BOOL  fmas_HasCard;
    char *fmas_CardUrl;          /* the linked article's own URL */
    char *fmas_CardTitle;
    char *fmas_CardDescription;
    char *fmas_CardProviderName; /* site name, e.g. "The Verge" */
    char *fmas_CardImageUrl;     /* "" if the card has no image (some sites provide none) */
} FS3ENetStatus;

/* Header of the flat timeline reply block.
 * Statuses follow immediately: (FS3ENetStatus *)(reply + 1)[i] */
typedef struct FS3ENetTimelineReply {
    ULONG fs3et_ViewModeBit;    /* echoed from request */
    ULONG fs3et_PageDirection; /* echoed from request, see FS3ENetPageDirection */
    ULONG fs3et_AccountGeneration; /* echoed from request, see FS3ENetTimelineReq */
    ULONG fs3et_ResponseShape; /* echoed from request, see FS3ENetTimelineShape */
    ULONG fs3et_Count;
    char *fs3et_RefreshPostId; /* echoes request's (repurposed) fs3et_MinId when
                                 * fs3et_ResponseShape is FS3ENET_TLSHAPE_SINGLE_REFRESH;
                                 * "" for every other shape. See FS3ENetTimelineReq's
                                 * doc comment on fs3et_MinId. */
    /* FS3ENetStatus[fs3et_Count] follows immediately in memory */
} FS3ENetTimelineReply;

/*
 * FS3ENETQ_POST_STATUS — publish a new status (toot).
 *
 * fs3ep_Spoiler is the CW/subject text; pass "" for no content warning.
 * fs3ep_InReplyToId is the status this replies to, "" for a standalone
 * toot -- see FS3EMastodon_PostStatus.
 * On FS3ENETR_OK, fs3em_Data is replaced with an FS3ENetPostStatusReply.
 */
typedef struct FS3ENetPostStatusReq {
    char *fs3ep_ApiBaseUrl;
    char *fs3ep_AccessToken;
    char *fs3ep_Content;     /* UTF-8 post body */
    char *fs3ep_Visibility;  /* "public", "unlisted", "private", "direct" */
    ULONG fs3ep_Sensitive;   /* TRUE/FALSE -- Mastodon's `sensitive` flag, applies to
                               * the whole status incl. every attachment; see
                               * FS3ETootView's sensitiveCheck in fs3etootview.h */
    char *fs3ep_Spoiler;     /* CW text; "" = no content warning */
    char *fs3ep_InReplyToId; /* status being replied to; "" = standalone toot */
    char *fs3ep_QuoteApprovalPolicy; /* "public", "followers", "nobody" */
    char *fs3ep_QuotedStatusId; /* status being quoted; "" = not a quote post */
    char *fs3ep_MediaIds[FS3ENET_MAX_MEDIA]; /* ids from a prior FS3ENETQ_UPLOAD_MEDIA
                               * reply -- see fs3etootview.c/friendsh3ep.c's
                               * GID_TOOT_SEND_BUTTON, which fires UPLOAD_MEDIA
                               * first when an attachment is set and only sends
                               * this request once that reply's media id is in
                               * hand. */
    ULONG fs3ep_MediaCount;
    char *fs3ep_Language;    /* ISO 639 code (e.g. "en"), "" = unspecified -- see
                               * FS3ETootView_GetLanguage/FS3EMastodon_PostStatus */

    /* Poll ("survey") to attach to this status -- fs3ep_PollOptionCount==0
     * means no poll (the common case, and the only option when
     * fs3ep_MediaCount>0: Mastodon itself disallows a status having both a
     * poll and attached media, same mutual exclusion as read-side
     * FS3ENetStatus.fmas_PollOptionCount/fmas_MediaCount above). Options
     * past fs3ep_PollOptionCount are NULL. See FS3ETootView's
     * pollOptionEditor[]/pollExpirationChooser (fs3etootview.h) for where
     * these come from on the compose side. */
    char  *fs3ep_PollOptions[FS3ENET_MAX_POLL_OPTIONS];
    ULONG  fs3ep_PollOptionCount;
    ULONG  fs3ep_PollExpiresIn; /* seconds until the poll closes; meaningless
                                  * when fs3ep_PollOptionCount==0 */
    BOOL   fs3ep_PollMultiple;  /* TRUE = multiple-choice poll (Mastodon's
                                  * poll[multiple]); meaningless when
                                  * fs3ep_PollOptionCount==0 -- see
                                  * FS3ETootView_GetPollMultiple */
} FS3ENetPostStatusReq;

FS3ENetPostStatusReq *FS3ENetPostStatusReq_Alloc(
    const char *apiBaseUrl, const char *accessToken,
    const char *content, const char *visibility, BOOL sensitive, const char *spoiler,
    const char *inReplyToId, const char *quoteApprovalPolicy,
    const char *quotedStatusId,
    const char *const *mediaIds, ULONG mediaCount,
    const char *language,
    const char *const *pollOptions, ULONG pollOptionCount, ULONG pollExpiresIn,
    BOOL pollMultiple);

typedef struct FS3ENetPostStatusReply {
    char *fs3ep_StatusId; /* new status id string */
} FS3ENetPostStatusReply;

/*
 * FS3ENETQ_UPLOAD_MEDIA — POST /api/v2/media, upload one image/audio/video
 * file as the attachment for a toot about to be composed. fs3eum_FilePath is
 * a local Amiga path (e.g. straight from a GETFILE gadget's GETFILE_File +
 * GETFILE_Drawer) -- read from disk by the network process itself, off the
 * GUI task, same reasoning FS3ENETQ_FETCH_IMAGE keeps file I/O off the GUI
 * task. fs3eum_MimeType is derived by the caller from the file's extension
 * (see FS3ETOOT_ATTACH_MEDIA_PATTERN in fs3etootview.c for the accepted
 * list) since Mastodon's media endpoint needs a real Content-Type, not a
 * guess made server-side. On FS3ENETR_OK, fs3em_Data is replaced with an
 * FS3ENetUploadMediaReply carrying the new attachment's id.
 *
 * No progress reporting (unlike FS3ENETQ_FETCH_IMAGE's chunked download) --
 * this is one blocking FS3EHttp_Post() of the whole file, same "single
 * exchange, no timeout" tradeoff FS3ENETQ_POST_STATUS/LOGIN_FINISH already
 * accept (see FS3EHTTP_TIMEOUT_SECS's comment in fs3enet_http.c). The GUI
 * caps the file size before ever sending this (see
 * FS3ENET_UPLOAD_MAX_BYTES) specifically so this exchange stays bounded in
 * practice.
 */
typedef struct FS3ENetUploadMediaReq {
    char *fs3eum_ApiBaseUrl;
    char *fs3eum_AccessToken;
    char *fs3eum_FilePath; /* local path to read and upload */
    char *fs3eum_MimeType; /* e.g. "image/jpeg" -- see FS3ETootAttachMimeType() */
} FS3ENetUploadMediaReq;

FS3ENetUploadMediaReq *FS3ENetUploadMediaReq_Alloc(
    const char *apiBaseUrl, const char *accessToken,
    const char *filePath, const char *mimeType);

typedef struct FS3ENetUploadMediaReply {
    char *fs3eum_MediaId; /* new media attachment id string */
} FS3ENetUploadMediaReply;

/*
 * FS3ENETQ_EDIT_STATUS — PUT /api/v1/statuses/:id, edit an existing status'
 * text (own toots only). fs3ee_MediaIds[0..fs3ee_MediaCount) are the
 * status' existing attachment ids (see FS3ENetStatus.fmas_MediaIds),
 * resent unchanged so the edit doesn't strip them -- see
 * FS3EMastodon_EditStatus. No spoiler/visibility fields: Mastodon's edit
 * endpoint doesn't accept changing either. On FS3ENETR_OK, fs3em_Data is
 * replaced with an FS3ENetEditStatusReply (same free/replace convention as
 * every other request in this file -- the GUI side always expects
 * fs3em_Data to end up a freshly allocated reply block, never the original
 * request left in place).
 */
typedef struct FS3ENetEditStatusReq {
    char *fs3ee_ApiBaseUrl;
    char *fs3ee_AccessToken;
    char *fs3ee_StatusId;
    char *fs3ee_Content;     /* UTF-8 new post body */
    char *fs3ee_MediaIds[FS3ENET_MAX_MEDIA];
    ULONG fs3ee_MediaCount;
} FS3ENetEditStatusReq;

FS3ENetEditStatusReq *FS3ENetEditStatusReq_Alloc(
    const char *apiBaseUrl, const char *accessToken,
    const char *statusId, const char *content,
    const char *const *mediaIds, ULONG mediaCount);

typedef struct FS3ENetEditStatusReply {
    char *fs3ee_StatusId; /* echoes the edited status id back */
} FS3ENetEditStatusReply;

/*
 * FS3ENETQ_UPDATE_BIO — PATCH /api/v1/accounts/update_credentials, set the
 * connected user's own profile bio -- see FS3EMastodon_UpdateBio. On
 * FS3ENETR_OK, fs3em_Data is replaced with an FS3ENetUpdateBioReply.
 */
typedef struct FS3ENetUpdateBioReq {
    char *fs3eub_ApiBaseUrl;
    char *fs3eub_AccessToken;
    char *fs3eub_Note; /* UTF-8 new bio text */
} FS3ENetUpdateBioReq;

FS3ENetUpdateBioReq *FS3ENetUpdateBioReq_Alloc(
    const char *apiBaseUrl, const char *accessToken, const char *note);

typedef struct FS3ENetUpdateBioReply {
    char *fs3eub_Note; /* server-echoed bio, HTML-stripped (see
                         * FS3ENet_HandleUpdateBio) -- ready to show as-is,
                         * same convention as every other timeline/profile
                         * bio text. */
} FS3ENetUpdateBioReply;

/*
 * FS3ENETQ_DELETE_STATUS — DELETE /api/v1/statuses/:id, delete an existing
 * status (own toots only). On FS3ENETR_OK, fs3em_Data is replaced with an
 * FS3ENetDeleteStatusReply echoing the deleted status id back -- the GUI
 * needs it to remove that post from TootTimeline's in-memory data (see
 * TTIMELINE_RemovePost), same reasoning FS3ENetFavouriteReply already
 * echoes its id for TTIMELINE_UpdatePost.
 */
typedef struct FS3ENetDeleteStatusReq {
    char *fs3ed_ApiBaseUrl;
    char *fs3ed_AccessToken;
    char *fs3ed_StatusId;
} FS3ENetDeleteStatusReq;

FS3ENetDeleteStatusReq *FS3ENetDeleteStatusReq_Alloc(
    const char *apiBaseUrl, const char *accessToken, const char *statusId);

typedef struct FS3ENetDeleteStatusReply {
    char *fs3ed_StatusId;
} FS3ENetDeleteStatusReply;

/* Mastodon Notification.type, mapped from the JSON string. FOLLOW/
 * FOLLOW_REQUEST carry no status (see FS3ENetNotification.fen_HasStatus);
 * every other value does. UNKNOWN covers the admin-only types (sign-up,
 * report, severed relationships, moderation warning) and anything else
 * this app doesn't specifically handle -- rendered with no actor/verb
 * prefix rather than guessed at. */
enum FS3ENetNotifType
{
    FS3ENOTIF_MENTION = 0,
    FS3ENOTIF_REBLOG,
    FS3ENOTIF_FAVOURITE,
    FS3ENOTIF_FOLLOW,
    FS3ENOTIF_FOLLOW_REQUEST,
    FS3ENOTIF_POLL,
    FS3ENOTIF_UPDATE,
    FS3ENOTIF_UNKNOWN
};

/*
 * FS3ENETQ_NOTIFICATIONS — GET /api/v1/notifications.
 *
 * No fs3en_ViewModeBit -- this queue always targets VIEWMODE_Notifs, only
 * one channel is ever meaningful for it, unlike FS3ENETQ_TIMELINE which is
 * shared across every channel including Search's several sub-modes.
 * fs3en_MaxId/MinId/PageDirection mirror FS3ENetTimelineReq exactly --
 * Mastodon paginates notifications by the notification's own id, the same
 * max_id/min_id query-param shape every timeline endpoint already uses.
 */
typedef struct FS3ENetNotificationsReq {
    ULONG  fs3en_PageDirection;     /* FS3ENetPageDirection; echoed in reply */
    ULONG  fs3en_AccountGeneration; /* opaque caller token; echoed in reply */
    char  *fs3en_ApiBaseUrl;
    char  *fs3en_AccessToken;
    char  *fs3en_MaxId;             /* "" = no lower bound */
    char  *fs3en_MinId;             /* "" = no upper bound */
} FS3ENetNotificationsReq;

FS3ENetNotificationsReq *FS3ENetNotificationsReq_Alloc(ULONG pageDirection,
    ULONG accountGeneration, const char *apiBaseUrl, const char *accessToken,
    const char *maxId, const char *minId);

/* One notification entry inside a FS3ENetNotificationsReply.
 * All char * fields point into the same flat block, same convention as
 * FS3ENetStatus -- one FreeVec() on the enclosing reply frees everything,
 * including fen_Status's own pointer fields (fen_Status is embedded by
 * value, not pointed-to, precisely so this holds). */
typedef struct FS3ENetNotification {
    char  *fen_Id;               /* notification's own id -- see TTLPost.postId's
                                   * doc comment on why this, not the status id, drives pagination */
    ULONG  fen_Type;             /* FS3ENetNotifType */
    char  *fen_ActorDisplayName; /* who triggered this notification */
    char  *fen_ActorAcct;
    char  *fen_ActorAvatarURL;
    BOOL   fen_HasStatus;        /* FALSE for FOLLOW/FOLLOW_REQUEST */
    FS3ENetStatus fen_Status;    /* meaningful iff fen_HasStatus */
} FS3ENetNotification;

/* Header of the flat notifications reply block.
 * Notifications follow immediately: (FS3ENetNotification *)(reply + 1)[i] */
typedef struct FS3ENetNotificationsReply {
    ULONG fs3en_PageDirection;
    ULONG fs3en_AccountGeneration;
    ULONG fs3en_Count;
    /* FS3ENetNotification[fs3en_Count] follows immediately in memory */
} FS3ENetNotificationsReply;

/*
 * FS3ENETQ_ACCOUNTS_LIST — fetch a list of accounts: fuzzy account search
 * (GET /api/v2/search?type=accounts), or a user's followers/following
 * (GET /api/v1/accounts/:id/followers or .../following). Unlike
 * FS3ENETQ_TIMELINE, this is deliberately single-page only for now: real
 * pagination for these endpoints is driven by an RFC5988 Link: response
 * header this codebase's HTTP layer doesn't parse at all (FS3EHttpResponse
 * only exposes body/status) -- matches the same "no pagination yet"
 * scope FS3EApp_SearchWord's word/hashtag search already accepted.
 */
enum FS3ENetAccountsListKind
{
    FS3ENET_ACCLIST_FOLLOWERS = 0, /* fs3eal_AccountId is whose followers to list */
    FS3ENET_ACCLIST_FOLLOWING,     /* fs3eal_AccountId is whose following to list */
    FS3ENET_ACCLIST_SEARCH,        /* fs3eal_Query is the raw (unencoded) search text */
    FS3ENET_ACCLIST_BLOCKS,        /* GET /api/v1/blocks -- accounts the CONNECTED user has
                                     * blocked; fs3eal_AccountId/fs3eal_Query both "", there's
                                     * no "whose list" to name (it's always the connected
                                     * account's own). */
    FS3ENET_ACCLIST_FAVOURITED_BY, /* GET /api/v1/statuses/:id/favourited_by -- fs3eal_AccountId
                                     * is reused (same field, different meaning) as the STATUS
                                     * id whose favouriters to list, from the Timeline menu's
                                     * "Who Faved that Toot" (see FS3EApp_ShowFavouritedBy,
                                     * TTIMELINE_SelectedPostId). Single page only, same
                                     * limitation as every other kind here -- Mastodon's Link
                                     * header pagination isn't followed. */
    FS3ENET_ACCLIST_REBLOGGED_BY   /* GET /api/v1/statuses/:id/reblogged_by -- same reuse of
                                     * fs3eal_AccountId as a status id, for "Who Boosted that
                                     * Toot" (FS3EApp_ShowRebloggedBy). */
};

typedef struct FS3ENetAccountsListReq {
    ULONG  fs3eal_Kind;              /* FS3ENetAccountsListKind; echoed in reply */
    ULONG  fs3eal_AccountGeneration; /* opaque caller token; echoed in reply, same
                                       * reasoning as FS3ENetTimelineReq's own field */
    char  *fs3eal_ApiBaseUrl;
    char  *fs3eal_AccessToken;
    char  *fs3eal_AccountId; /* FOLLOWERS/FOLLOWING: whose list; FAVOURITED_BY/REBLOGGED_BY:
                               * the STATUS id instead (see those kinds' own comments); ""
                               * for SEARCH/BLOCKS */
    char  *fs3eal_Query;     /* SEARCH: raw (unencoded) query text; "" otherwise */
} FS3ENetAccountsListReq;

FS3ENetAccountsListReq *FS3ENetAccountsListReq_Alloc(ULONG kind,
    ULONG accountGeneration, const char *apiBaseUrl, const char *accessToken,
    const char *accountId, const char *query);

/* Header of the flat accounts-list reply block.
 * FS3EMastodonAccount[fs3eal_Count] follows immediately in memory. */
typedef struct FS3ENetAccountsListReply {
    ULONG fs3eal_Kind;              /* echoed from request, see FS3ENetAccountsListKind */
    ULONG fs3eal_AccountGeneration; /* echoed from request, see FS3ENetAccountsListReq */
    ULONG fs3eal_Count;
} FS3ENetAccountsListReply;

/*
 * FS3ENETQ_FAVORITE — POST /api/v1/statuses/:id/favourite or .../unfavourite.
 *
 * fs3efa_Favourite selects which: TRUE = favourite, FALSE = unfavourite.
 * On FS3ENETR_OK, fs3em_Data is replaced with an FS3ENetFavouriteReply
 * carrying just the server-confirmed favourited boolean -- deliberately
 * NOT that response's replies_count/reblogs_count/favourites_count too:
 * those looked like a free, always-fresh echo of the whole toot's counts,
 * but weren't reliably present on every instance's response in practice,
 * and blindly copying them across zeroed this toot's OTHER counts
 * (Reply/Boost) on every single favourite toggle. See
 * FS3EMastodon_Favourite's comment. The resulting favourites_count is a
 * local +1/-1 delta the GUI applies itself -- see
 * TTIMELINE_UpdatePost/TTL_POSTUPD_FAVOURITED in fs3etoottimeline.h.
 */
typedef struct FS3ENetFavouriteReq {
    char *fs3efa_ApiBaseUrl;
    char *fs3efa_AccessToken;
    char *fs3efa_StatusId;
    BOOL  fs3efa_Favourite;   /* TRUE=favourite, FALSE=unfavourite */
} FS3ENetFavouriteReq;

FS3ENetFavouriteReq *FS3ENetFavouriteReq_Alloc(
    const char *apiBaseUrl, const char *accessToken,
    const char *statusId, BOOL favourite);

typedef struct FS3ENetFavouriteReply {
    char  *fs3efa_StatusId;
    BOOL   fs3efa_Favourited;
} FS3ENetFavouriteReply;

/*
 * FS3ENETQ_BOOKMARK — POST /api/v1/statuses/:id/bookmark or .../unbookmark.
 *
 * Same shape as FS3ENETQ_FAVORITE -- fs3ebk_Bookmark selects which: TRUE =
 * bookmark, FALSE = unbookmark. On FS3ENETR_OK, fs3em_Data is replaced with
 * an FS3ENetBookmarkReply carrying just the server-confirmed bookmarked
 * boolean. Unlike Favorite/Reblog there's no count to derive a delta for --
 * Mastodon never exposes a public bookmarksCount -- so the GUI just
 * overwrites TTLPost.bookmarked, see TTL_POSTUPD_BOOKMARKED.
 *
 * fs3ebk_CacheDir, if non-"", is this account's local offline-bookmarks
 * directory (see FS3EApp_BookmarksCacheDir in fs3eaccounts.h) -- on a
 * confirmed bookmark, the raw status JSON the server just returned is
 * written there as <statusId>.json (see FS3EMastodon_Bookmark's
 * outRawStatusJson); on a confirmed unbookmark, that file is deleted if
 * present. "" disables caching for this call (e.g. the account has no id
 * yet, see FS3EApp_BookmarksCacheDir's own FALSE case) without failing the
 * toggle itself -- caching is a bonus, not a precondition for bookmarking.
 */
typedef struct FS3ENetBookmarkReq {
    char *fs3ebk_ApiBaseUrl;
    char *fs3ebk_AccessToken;
    char *fs3ebk_StatusId;
    BOOL  fs3ebk_Bookmark;   /* TRUE=bookmark, FALSE=unbookmark */
    char *fs3ebk_CacheDir;
} FS3ENetBookmarkReq;

FS3ENetBookmarkReq *FS3ENetBookmarkReq_Alloc(
    const char *apiBaseUrl, const char *accessToken,
    const char *statusId, BOOL bookmark, const char *cacheDir);

typedef struct FS3ENetBookmarkReply {
    char  *fs3ebk_StatusId;
    BOOL   fs3ebk_Bookmarked;
} FS3ENetBookmarkReply;

/*
 * FS3ENETQ_VOTE_POLL — POST /api/v1/polls/:id/votes.
 *
 * fs3evp_PollId is the poll's OWN id (FS3ENetStatus.fmas_PollId /
 * TTLPost.pollId), NOT the status id -- Mastodon's vote endpoint addresses
 * the poll object directly. fs3evp_ChoiceIndex is the 0-based option index
 * (TTL_HOT_POLL_VOTE's hot-spot data is the 1-based option number the GUI
 * shows the user -- friendsh3ep.c subtracts 1 before building this
 * request). Sent as a single-element JSON array ("choices":[N]) -- this
 * app's poll UI only supports single-choice voting (see FS3ETootView's
 * poll compose window, no "multiple choice" option either), so there's
 * never more than one index to send.
 *
 * fs3evp_StatusId travels through unchanged, carried only so the reply
 * handler (fs3erequests.c) knows which status to re-fetch on success --
 * see FS3ENetVotePollReply. On FS3ENETR_OK, fs3em_Data is replaced with
 * one; the vote itself carries no useful body worth keeping (Mastodon
 * returns the updated Poll object, but re-fetching the whole status is
 * simpler and reuses the exact SINGLE_REFRESH plumbing
 * FS3EApp_RefreshVisibleToots/F5 already has, rather than a second,
 * poll-only patch path).
 */
typedef struct FS3ENetVotePollReq {
    char *fs3evp_ApiBaseUrl;
    char *fs3evp_AccessToken;
    char *fs3evp_PollId;
    char *fs3evp_StatusId;
    ULONG fs3evp_ChoiceIndex;
} FS3ENetVotePollReq;

FS3ENetVotePollReq *FS3ENetVotePollReq_Alloc(
    const char *apiBaseUrl, const char *accessToken,
    const char *pollId, const char *statusId, ULONG choiceIndex);

typedef struct FS3ENetVotePollReply {
    char *fs3evp_StatusId; /* echoed from the request, see above */
} FS3ENetVotePollReply;

/*
 * FS3ENETQ_DOMAIN_BLOCKS — GET /api/v1/domain_blocks: the connected
 * account's own blocked servers. Unlike every other list request in this
 * file, the server's reply is a bare array of plain domain strings, not
 * Account/Status objects -- FS3ENet_HandleDomainBlocks (fs3enet.c) still
 * reuses FS3EMastodon_GetTimeline(..., FS3ENET_TLSHAPE_ARRAY, ...) to fetch
 * it (that function only checks "is the top-level JSON a plain array", it
 * has no opinion on what its elements are), just walks the array expecting
 * cJSON string items instead of object items. Single page only, same
 * "no RFC5988 Link: header parsing" limitation as FS3ENETQ_ACCOUNTS_LIST.
 */
typedef struct FS3ENetDomainBlocksReq {
    ULONG fs3edb_AccountGeneration; /* opaque caller token; echoed in reply */
    char *fs3edb_ApiBaseUrl;
    char *fs3edb_AccessToken;       /* always required -- there is no anonymous
                                      * "someone else's domain blocks" to view */
} FS3ENetDomainBlocksReq;

FS3ENetDomainBlocksReq *FS3ENetDomainBlocksReq_Alloc(ULONG accountGeneration,
    const char *apiBaseUrl, const char *accessToken);

/* Header of the flat domain-blocks reply block. Immediately following in
 * memory: fs3edb_Count char* pointers (an array of pointers, NOT a fixed
 * cap like FS3ENetPostStatusReq.fs3ep_PollOptions), each pointing into the
 * packed domain-string bytes that follow THAT array -- same "typed array
 * then trailing string pool" shape FS3ENetAccountsListReply uses for
 * FS3EMastodonAccount[], just with a bare char* as the "element" instead
 * of a whole struct. */
typedef struct FS3ENetDomainBlocksReply {
    ULONG fs3edb_AccountGeneration; /* echoed from request */
    ULONG fs3edb_Count;
} FS3ENetDomainBlocksReply;

/*
 * FS3ENETQ_BOOKMARKS_LOCAL — list a page of the on-disk offline-bookmarks
 * cache (see FS3ENETQ_BOOKMARK's own doc comment on fs3ebk_CacheDir), no
 * server round trip at all. Real Mastodon pagination for GET /api/v1/
 * bookmarks needs an RFC5988 Link: response header this codebase's HTTP
 * layer doesn't parse (same limitation FS3ENETQ_ACCOUNTS_LIST's own doc
 * comment already flags for followers/following/search) -- reading our OWN
 * local directory sidesteps that entirely: we control the ordering (by
 * fib_Date, newest-bookmarked-first) and the cursor (a plain skip count),
 * so paging never needs the server's cooperation once a toot is cached.
 *
 * fs3ebl_Offset/fs3ebl_Limit: skip the first Offset entries (by that same
 * newest-first order), then return up to Limit of what's left.
 * fs3ebl_PageDirection: FS3ENETPAGE_INITIAL/OLDER only, same "no NEWER"
 * reasoning as FS3ENETQ_NEWS (there's no live-updating concept here either
 * -- new bookmarks are added at the *front* by FS3ENETQ_BOOKMARK's own
 * cache write, not discovered via a page fetch). Echoed in the reply so the
 * GUI's generic AddPost/AppendPost dispatch can tell initial from paginated
 * without tracking it separately.
 *
 * On FS3ENETR_OK, fs3em_Data is replaced with a flat FS3ENetBookmarksLocalReply
 * block, FS3ENetStatus[fs3ebl_Count] following immediately -- same shape
 * FS3ENETQ_TIMELINE's own reply uses, reusing FS3EApp_MapStatusToPostSetup
 * verbatim on the GUI side.
 */
typedef struct FS3ENetBookmarksLocalReq {
    ULONG fs3ebl_PageDirection;
    ULONG fs3ebl_AccountGeneration;
    ULONG fs3ebl_Offset;
    ULONG fs3ebl_Limit;
    char *fs3ebl_CacheDir;
} FS3ENetBookmarksLocalReq;

FS3ENetBookmarksLocalReq *FS3ENetBookmarksLocalReq_Alloc(ULONG pageDirection,
    ULONG accountGeneration, ULONG offset, ULONG limit, const char *cacheDir);

typedef struct FS3ENetBookmarksLocalReply {
    ULONG fs3ebl_PageDirection;
    ULONG fs3ebl_AccountGeneration;
    ULONG fs3ebl_Count;
    /* FS3ENetStatus[fs3ebl_Count] follows immediately in memory */
} FS3ENetBookmarksLocalReply;

/*
 * FS3ENETQ_BOOKMARKS_SYNC — one-shot backfill of the on-disk offline-
 * bookmarks cache from the server's own first page (GET /api/v1/
 * bookmarks?limit=40, the endpoint's own max) -- fired once per session
 * right before the first FS3ENETQ_BOOKMARKS_LOCAL fetch (see
 * FS3EApp_FetchTimeline's VIEWMODE_Bookmarks branch), so bookmarks made on
 * another client (or before this cache existed) show up locally too, not
 * just ones made through this app's own TTL_HOT_BOOKMARK. Anything already
 * cached (by statusId, checked via a plain Lock() probe) is left alone --
 * this only ever ADDS files, never overwrites/removes. Deliberately not a
 * general resync: bookmarks beyond that first ~40 that were never touched
 * through this app won't backfill -- see this request's own doc comment in
 * fs3erequests.c for the accepted trade-off (avoiding the same Link-header
 * pagination this whole cache was built to route around).
 *
 * On FS3ENETR_OK, fs3em_Data is replaced with an FS3ENetBookmarksSyncReply
 * carrying how many NEW files this call wrote (0 is a normal, common
 * result, not an error). Failure (offline, etc.) is silently tolerated by
 * the GUI -- see its own reply handler -- since the local cache may still
 * have plenty to show even with no network at all.
 */
typedef struct FS3ENetBookmarksSyncReq {
    char *fs3ebs_ApiBaseUrl;
    char *fs3ebs_AccessToken;
    char *fs3ebs_CacheDir;
} FS3ENetBookmarksSyncReq;

FS3ENetBookmarksSyncReq *FS3ENetBookmarksSyncReq_Alloc(
    const char *apiBaseUrl, const char *accessToken, const char *cacheDir);

typedef struct FS3ENetBookmarksSyncReply {
    ULONG fs3ebs_NewCount;
} FS3ENetBookmarksSyncReply;

/*
 * FS3ENETQ_REBLOG — POST /api/v1/statuses/:id/reblog or .../unreblog.
 *
 * Same shape and same "don't trust echoed counts" reasoning as
 * FS3ENETQ_FAVORITE above -- fs3ere_Reblog selects which: TRUE = reblog,
 * FALSE = unreblog. On FS3ENETR_OK, fs3em_Data is replaced with an
 * FS3ENetReblogReply carrying just the server-confirmed reblogged boolean;
 * the resulting reblogs_count is a local +1/-1 delta the GUI applies
 * itself -- see TTIMELINE_UpdatePost/TTL_POSTUPD_REBLOGGED in
 * fs3etoottimeline.h.
 */
typedef struct FS3ENetReblogReq {
    char *fs3ere_ApiBaseUrl;
    char *fs3ere_AccessToken;
    char *fs3ere_StatusId;
    BOOL  fs3ere_Reblog;   /* TRUE=reblog, FALSE=unreblog */
} FS3ENetReblogReq;

FS3ENetReblogReq *FS3ENetReblogReq_Alloc(
    const char *apiBaseUrl, const char *accessToken,
    const char *statusId, BOOL reblog);

typedef struct FS3ENetReblogReply {
    char  *fs3ere_StatusId;
    BOOL   fs3ere_Reblogged;
} FS3ENetReblogReply;

/*
 * FS3ENETQ_ACCOUNT_LOOKUP — GET /api/v1/accounts/lookup?acct=<acct>.
 * The entry point for opening a profile view (see TootTimeline's
 * TTIMELINE_ShowProfile): resolves an acct string ("user" or
 * "user@instance", no leading '@') to a full account. fs3eal_AccessToken
 * may be "" (unauthenticated lookup works for public accounts).
 *
 * On FS3ENETR_OK, fs3em_Data is replaced with an FS3ENetAccountLookupReply.
 */
typedef struct FS3ENetAccountLookupReq {
    char *fs3eal_ApiBaseUrl;
    char *fs3eal_AccessToken;
    char *fs3eal_Acct;
} FS3ENetAccountLookupReq;

FS3ENetAccountLookupReq *FS3ENetAccountLookupReq_Alloc(
    const char *apiBaseUrl, const char *accessToken, const char *acct);

typedef struct FS3ENetAccountLookupReply {
    FS3EMastodonAccount fs3eal_Account; /* fma_Note here is HTML-stripped, unlike FS3EMastodon_LookupAccount's raw output */
} FS3ENetAccountLookupReply;

/*
 * FS3ENETQ_RELATIONSHIP — GET /api/v1/accounts/relationships?id[]=<id>.
 * On FS3ENETR_OK, fs3em_Data is replaced with an FS3ENetRelationshipReply.
 */
typedef struct FS3ENetRelationshipReq {
    char *fs3erl_ApiBaseUrl;
    char *fs3erl_AccessToken;
    char *fs3erl_AccountId;
} FS3ENetRelationshipReq;

FS3ENetRelationshipReq *FS3ENetRelationshipReq_Alloc(
    const char *apiBaseUrl, const char *accessToken, const char *accountId);

typedef struct FS3ENetRelationshipReply {
    char *fs3erl_AccountId;
    BOOL  fs3erl_Following;
    BOOL  fs3erl_Blocking;  /* connected user currently blocks this account --
                              * drives the profile header's Unblock button, see
                              * TTL_HOT_UNBLOCK/TTIMELINE_UpdateProfileBlocked. */
} FS3ENetRelationshipReply;

/*
 * FS3ENETQ_RELATIONSHIPS — GET /api/v1/accounts/relationships?id[]=<id>&id[]=<id>...,
 * one repeated id[] per account. Batch counterpart of FS3ENETQ_RELATIONSHIP
 * above: fired after an FS3ENETQ_ACCOUNTS_LIST reply lands, covering every
 * account id just added to the list (minus the connected user's own id --
 * Mastodon's relationships endpoint has no self-relationship to report), so
 * TTLAccountRow_Class rows can show a "Follows you" badge (see
 * TTL_POSTUPD_RELATIONSHIP in fs3etoottimeline.h). Unlike
 * FS3ENetRelationshipReply, this one also carries followed_by -- the
 * singular request/reply above only ever needed the connected user's own
 * following state for the profile header's Follow/Unfollow button, this one
 * needs both directions to know if the OTHER account follows back.
 *
 * A char*[fs3erls_Count] pointer array follows the header fields
 * immediately in memory (each entry pointing further into the same
 * AllocVec block, at the id string bytes packed after the array itself) --
 * same "pointer array then string bytes" layout FS3ENet_HandleAccountsList's
 * own reply already uses for its FS3EMastodonAccount[] trailing array.
 *
 * On FS3ENETR_OK, fs3em_Data is replaced with an FS3ENetRelationshipsReply.
 */
typedef struct FS3ENetRelationshipsReq {
    ULONG  fs3erls_AccountGeneration; /* opaque caller token; echoed in reply */
    ULONG  fs3erls_Count;
    char  *fs3erls_ApiBaseUrl;
    char  *fs3erls_AccessToken;
    /* char *fs3erls_AccountIds[fs3erls_Count] follows immediately */
} FS3ENetRelationshipsReq;

FS3ENetRelationshipsReq *FS3ENetRelationshipsReq_Alloc(
    ULONG accountGeneration, const char *apiBaseUrl, const char *accessToken,
    const char *const *accountIds, ULONG count);

typedef struct FS3ENetRelationshipEntry {
    char *fs3erle_AccountId;
    BOOL  fs3erle_Following;
    BOOL  fs3erle_FollowedBy;
} FS3ENetRelationshipEntry;

/* Header of the flat relationships reply block.
 * FS3ENetRelationshipEntry[fs3erls_Count] follows immediately in memory. */
typedef struct FS3ENetRelationshipsReply {
    ULONG fs3erls_AccountGeneration; /* echoed from request */
    ULONG fs3erls_Count;
} FS3ENetRelationshipsReply;

/*
 * FS3ENETQ_FOLLOW — POST /api/v1/accounts/:id/follow or .../unfollow.
 *
 * fs3efo_Follow selects which: TRUE = follow, FALSE = unfollow. On
 * FS3ENETR_OK, fs3em_Data is replaced with an FS3ENetFollowReply carrying
 * just the server-confirmed following boolean -- same "don't trust
 * anything beyond the one confirmed flag" rule as FS3ENETQ_FAVORITE (see
 * FS3ENetFavouriteReply's comment); the Relationship object this endpoint
 * returns doesn't even carry follower/following counts, so there's
 * nothing else to echo anyway. The resulting followers_count is a local
 * +1/-1 delta the GUI applies itself, mirroring TTL_POSTUPD_FAVOURITED.
 */
typedef struct FS3ENetFollowReq {
    char *fs3efo_ApiBaseUrl;
    char *fs3efo_AccessToken;
    char *fs3efo_AccountId;
    BOOL  fs3efo_Follow;   /* TRUE=follow, FALSE=unfollow */
} FS3ENetFollowReq;

FS3ENetFollowReq *FS3ENetFollowReq_Alloc(
    const char *apiBaseUrl, const char *accessToken,
    const char *accountId, BOOL follow);

typedef struct FS3ENetFollowReply {
    char *fs3efo_AccountId;
    BOOL  fs3efo_Following;
} FS3ENetFollowReply;

/*
 * FS3ENETQ_BLOCK — POST /api/v1/accounts/:id/block, from the User menu's
 * "Block user" item (see Action_UserBlock/Action_ToggleBlock). On
 * FS3ENETR_OK, fs3em_Data is replaced with an FS3ENetBlockReply carrying
 * the server-confirmed following/blocking booleans (see
 * FS3EMastodon_Block's own comment on why following matters here too --
 * unlike FS3ENETQ_UNBLOCK, a block can also flip a still-showing
 * "Following" state).
 */
typedef struct FS3ENetBlockReq {
    char *fs3eblk_ApiBaseUrl;
    char *fs3eblk_AccessToken;
    char *fs3eblk_AccountId;
} FS3ENetBlockReq;

FS3ENetBlockReq *FS3ENetBlockReq_Alloc(
    const char *apiBaseUrl, const char *accessToken, const char *accountId);

typedef struct FS3ENetBlockReply {
    char *fs3eblk_AccountId;
    BOOL  fs3eblk_Following;
    BOOL  fs3eblk_Blocking;
} FS3ENetBlockReply;

/*
 * FS3ENETQ_UNBLOCK — POST /api/v1/accounts/:id/unblock. Unlike
 * FS3ENETQ_FOLLOW there is no toggle direction and nothing to echo back
 * beyond success/failure -- the caller already knows the resulting state
 * is "not blocked" (see FS3EMastodon_Unblock's own comment). On
 * FS3ENETR_OK, fs3em_Data is replaced with an FS3ENetUnblockReply
 * carrying just the account id, so the reply handler can target the
 * right profile header via TTIMELINE_UpdateProfileBlocked.
 */
typedef struct FS3ENetUnblockReq {
    char *fs3eub_ApiBaseUrl;
    char *fs3eub_AccessToken;
    char *fs3eub_AccountId;
} FS3ENetUnblockReq;

FS3ENetUnblockReq *FS3ENetUnblockReq_Alloc(
    const char *apiBaseUrl, const char *accessToken, const char *accountId);

typedef struct FS3ENetUnblockReply {
    char *fs3eub_AccountId;
} FS3ENetUnblockReply;

/*
 * FS3ENETQ_DOMAIN_BLOCK_STATE — GET /api/v1/domain_blocks, scanned for one
 * domain (see FS3EMastodon_IsDomainBlocked's own doc comment for the
 * single-page limitation). Fired once per "about this server" lookup to
 * drive the instance header's Block/Unblock button label. On FS3ENETR_OK,
 * fs3em_Data is replaced with an FS3ENetDomainBlockStateReply.
 */
typedef struct FS3ENetDomainBlockStateReq {
    char *fs3edbs_ApiBaseUrl;
    char *fs3edbs_AccessToken;
    char *fs3edbs_Domain;
} FS3ENetDomainBlockStateReq;

FS3ENetDomainBlockStateReq *FS3ENetDomainBlockStateReq_Alloc(
    const char *apiBaseUrl, const char *accessToken, const char *domain);

typedef struct FS3ENetDomainBlockStateReply {
    char *fs3edbs_Domain;
    BOOL  fs3edbs_Blocked;
} FS3ENetDomainBlockStateReply;

/*
 * FS3ENETQ_DOMAIN_BLOCK_TOGGLE — POST /api/v1/domain_blocks?domain=<domain>
 * (fs3edbt_Block=TRUE) or DELETE .../domain_blocks?domain=<domain>
 * (fs3edbt_Block=FALSE). Same "toggle direction chosen by the caller,
 * confirmed state echoed back" shape as FS3ENETQ_FOLLOW -- except here the
 * confirmed state is always just fs3edbt_Block itself (see
 * FS3EMastodon_ToggleDomainBlock's own doc comment: success guarantees
 * that state, nothing else to read back from the response body).
 */
typedef struct FS3ENetDomainBlockToggleReq {
    char *fs3edbt_ApiBaseUrl;
    char *fs3edbt_AccessToken;
    char *fs3edbt_Domain;
    BOOL  fs3edbt_Block;   /* TRUE=block, FALSE=unblock */
} FS3ENetDomainBlockToggleReq;

FS3ENetDomainBlockToggleReq *FS3ENetDomainBlockToggleReq_Alloc(
    const char *apiBaseUrl, const char *accessToken, const char *domain, BOOL block);

typedef struct FS3ENetDomainBlockToggleReply {
    char *fs3edbt_Domain;
    BOOL  fs3edbt_Blocked;
} FS3ENetDomainBlockToggleReply;

/*
 * FS3ENETQ_TRANSLATE_STATUS — POST /api/v1/statuses/:id/translate
 * (Mastodon 4.0+, see FS3EMastodon_TranslateStatus). fs3ets_TargetLang is
 * an ISO 639 code (see FS3EOSLocale_LanguageCode()); "" lets the server
 * fall back to its own default target.
 *
 * On FS3ENETR_OK, fs3em_Data is replaced with an FS3ENetTranslateStatusReply.
 * On FS3ENETR_HTTP_ERROR (translation not configured/supported for this
 * language/status), fs3em_Data still points at the original request block --
 * the GUI's reply handler (fs3erequests.c) simply leaves the toot showing
 * its original text, no error requester (translation failures are common
 * enough -- an unsupported language pair, a status too short to detect --
 * not worth interrupting the user over).
 */
typedef struct FS3ENetTranslateStatusReq {
    char *fs3ets_ApiBaseUrl;
    char *fs3ets_AccessToken;
    char *fs3ets_StatusId;
    char *fs3ets_TargetLang;
} FS3ENetTranslateStatusReq;

FS3ENetTranslateStatusReq *FS3ENetTranslateStatusReq_Alloc(
    const char *apiBaseUrl, const char *accessToken,
    const char *statusId, const char *targetLang);

typedef struct FS3ENetTranslateStatusReply {
    char *fs3ets_StatusId;         /* echoed from the request -- see
                                     * TTIMELINE_ApplyTranslation's postId match */
    char *fs3ets_TranslatedContent; /* HTML-stripped plain text, same convention
                                     * as FS3ENetStatus.fmas_Content */
} FS3ENetTranslateStatusReply;

/*
 * FS3ENETQ_NEWS — GET /api/v1/trends/links (Mastodon's "Explore/News"
 * trending-links list, VIEWMODE_News). fs3enw_AccessToken may be "" --
 * this endpoint is public, same as timelines/public.
 *
 * Paginated via the endpoint's own ?offset= (not a status-id, trending
 * links have no stable id to page by) -- same small ?limit= per page every
 * other channel already uses (see ViewModeTimeline's own "?limit=4"),
 * instead of the old one-shot "?limit=20" that fetched -- and fired every
 * one of its up to 20 card-image downloads -- all at once on open.
 * fs3enw_PageDirection is FS3ENETPAGE_INITIAL/OLDER only: trending links
 * aren't chronological, so there's no NEWER equivalent (mirrors
 * FS3ENetAccountsListReq's own followers/following pages, which are also
 * OLDER-only... except unlike those, News DOES keep paginating past its
 * first page, via fs3enw_Offset). fs3enw_AccountGeneration: same stale-
 * reply-after-account-switch guard as FS3ENetTimelineReq/
 * FS3ENetNotificationsReq.
 *
 * On FS3ENETR_OK, fs3em_Data is replaced with a flat FS3ENetNewsReply
 * block; fs3em_Data on error still points at the original request block.
 */
typedef struct FS3ENetNewsReq {
    ULONG fs3enw_PageDirection;     /* FS3ENetPageDirection; echoed in reply */
    ULONG fs3enw_AccountGeneration; /* opaque caller token; echoed in reply */
    ULONG fs3enw_Offset;            /* trends/links's own ?offset= paging cursor */
    char *fs3enw_ApiBaseUrl;
    char *fs3enw_AccessToken;
} FS3ENetNewsReq;

FS3ENetNewsReq *FS3ENetNewsReq_Alloc(ULONG pageDirection, ULONG accountGeneration,
    ULONG offset, const char *apiBaseUrl, const char *accessToken);

/* One trending-link entry. All char * fields point into the enclosing
 * FS3ENetNewsReply's own flat block -- one FreeVec() frees everything,
 * same convention as FS3ENetStatus. Fields mirror a toot's own embedded
 * link-preview card (FS3ENetStatus.fmas_Card*) -- this endpoint returns
 * the identical PreviewCard shape, just as bare top-level array entries
 * instead of nested inside a status -- plus fnn_PublishedAt, which only
 * trending-link entries carry (Mastodon 4.3+; "" on older servers). */
typedef struct FS3ENetNewsItem {
    char *fnn_Title;
    char *fnn_Description;
    char *fnn_Url;
    char *fnn_ProviderName;
    char *fnn_ImageUrl;
    char *fnn_PublishedAt; /* ISO 8601, "" if the server doesn't provide one */
} FS3ENetNewsItem;

/* Header of the flat news reply block.
 * FS3ENetNewsItem[fs3enw_Count] follows immediately in memory. */
typedef struct FS3ENetNewsReply {
    ULONG fs3enw_PageDirection;
    ULONG fs3enw_AccountGeneration;
    ULONG fs3enw_Count;
} FS3ENetNewsReply;

#endif /* FS3ENET_H */
