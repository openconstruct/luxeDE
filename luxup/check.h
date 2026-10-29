/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LUXUP_CHECK_H
#define LUXUP_CHECK_H
#include <glib.h>
#include <gio/gio.h>

typedef enum {
    LUXUP_SOURCE_APT = 0,
    LUXUP_SOURCE_FLATPAK = 1,
    LUXUP_SOURCE_SNAP = 2
} LuxUpSource;

typedef struct {
    int source;
    char *name;
    char *installed;
    char *available;
    char *scope;               /* Flatpak installation scope, otherwise empty. */
    char *ref;                 /* Stable Flatpak reference, otherwise empty. */
} LuxUpPackage;

typedef struct {
    GPtrArray *packages;        /* LuxUpPackage*, free with luxup_result_free */
    gboolean apt_available;     /* APT backend executable is installed. */
    gboolean snap_available;
    gboolean snap_hold_known;   /* The refresh-policy response was recognizable. */
    gboolean snap_auto_refresh_held;
    gboolean check_failed;      /* TRUE when a channel probe failed; result is unknown */
    gint64 apt_refreshed_at;    /* Unix seconds, 0 if no successful refresh is known. */
    char *apt_refresh_error;    /* NULL when not attempted or successful */
    char *apt_check_error;      /* NULL unless the APT listing is incomplete. */
    char *flatpak_user_error;
    char *flatpak_system_error;
    char *snap_check_error;
} LuxUpResult;

#define LUXUP_APT_FRESH_SECONDS (24 * 60 * 60)
static inline gboolean luxup_apt_lists_fresh(const LuxUpResult *result, gint64 now)
{
    return result && result->apt_available && result->apt_refreshed_at > 0 && now >= result->apt_refreshed_at &&
           now - result->apt_refreshed_at <= LUXUP_APT_FRESH_SECONDS;
}

/* Detect and enumerate pending updates.  The check is read-only.  When
 * refresh_apt is TRUE the shared LuxuryStore helper is invoked through pkexec
 * to refresh the APT lists first; failure is reported but never fatal.
 * A missing login agent is retried for up to 30 half-second intervals;
 * cancellation/denial and actual refresh failures are never retried. */
LuxUpResult *luxup_check(gboolean refresh_apt, GCancellable *cancellable);
void luxup_result_free(LuxUpResult *result);

const char *luxup_source_name(int source);
guint luxup_count(const LuxUpResult *result, int source);

#endif
