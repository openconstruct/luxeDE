#!/bin/sh
# Script used to install new icon theme.
# Copyright (C) 2008 paulliu, pcman
# Copyright (C) 2026 Jerry Howell for the Luxe hardening work.
# License: GNU GPL

# Install one icon theme from a .tar.gz/.tar.bz2 archive into the user's
# icon directory, then print the installed theme name on stdout.
# Diagnostics go to stderr; a non-zero exit means nothing was installed.
set -eu

if [ $# -ne 1 ]; then
    echo "Usage: $0 compressed_file" >&2
    exit 2
fi

ARCHIVE=$1
if [ -z "$ARCHIVE" ] || [ ! -f "$ARCHIVE" ] || [ -L "$ARCHIVE" ]; then
    echo "$ARCHIVE: not a regular archive file" >&2
    exit 1
fi

case "$ARCHIVE" in
    *.tar.gz|*.tar.bz2) ;;
    *) echo "$ARCHIVE: not a supported archive" >&2; exit 1 ;;
esac

# Private staging directory; mktemp failure must abort, never continue with
# an empty path (which previously turned into /members.txt under /).
TMPD=$(mktemp -d "${TMPDIR:-/tmp}/icon-theme-XXXXXXX") || {
    echo "Cannot create a temporary directory" >&2
    exit 1
}
STAGED=
cleanup() {
    if [ -n "$STAGED" ]; then rm -rf "$STAGED"; fi
    rm -rf "$TMPD"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

EXTRACT=$TMPD/extract
HELPER_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
EXTRACT_HELPER=$HELPER_DIR/luxe-theme-extract
if [ ! -x "$EXTRACT_HELPER" ]; then
    echo "Cannot find the archive extraction helper" >&2
    exit 1
fi
mkdir "$EXTRACT"
if ! timeout 30s "$EXTRACT_HELPER" "$ARCHIVE" "$EXTRACT"; then
    echo "$ARCHIVE: archive validation or extraction failed" >&2
    exit 1
fi

# XDG rules: a relative XDG_DATA_HOME must be ignored, otherwise the theme
# would land somewhere the application never scans.
if [ -z "${XDG_DATA_HOME:-}" ] || [ "${XDG_DATA_HOME#/}" = "$XDG_DATA_HOME" ]; then
    if [ -z "${HOME:-}" ]; then
        echo "HOME is not set; cannot determine the user icon directory" >&2
        exit 1
    fi
    XDG_DATA_HOME=$HOME/.local/share
fi
THEME_DIR=$XDG_DATA_HOME/icons
mkdir -p "$THEME_DIR"
THEME_DIR=$(CDPATH= cd -- "$THEME_DIR" && pwd -P) || {
    echo "Cannot resolve the user icon-theme directory" >&2
    exit 1
}
PUBLISH_HELPER=$HELPER_DIR/luxe-theme-publish
if [ ! -x "$PUBLISH_HELPER" ]; then
    echo "Cannot find the icon-theme publication helper" >&2
    exit 1
fi

installed=0
theme_roots=0
reported=0
report_failure() {
    if [ "$reported" -lt 16 ]; then
        echo "$1" >&2
        reported=$((reported + 1))
    fi
}
# Only top-level theme directories count. A nested index.theme belongs to
# the same theme and must not be installed a second time.
for dir in "$EXTRACT"/*; do
    [ -d "$dir" ] || continue
    [ -L "$dir" ] && continue
    [ -f "$dir/index.theme" ] || continue

    theme_roots=$((theme_roots + 1))
    if [ "$theme_roots" -gt 16 ]; then
        report_failure "$ARCHIVE: more than 16 theme roots; remaining entries were skipped"
        break
    fi

    NAME=$(basename "$dir")
    case "$NAME" in
        ''|.|..|*/*) continue ;;
        *[!A-Za-z0-9._+-]*)
            report_failure "$ARCHIVE: theme root has unsupported name characters; entry skipped"
            continue ;;
    esac
    # Reject a destination that is a symlink, including a dangling one.
    if [ -L "$THEME_DIR/$NAME" ] || [ -e "$THEME_DIR/$NAME" ]; then
        report_failure "$NAME: a theme with this name already exists; not replaced"
        continue
    fi
    # Stage on the destination filesystem, then publish without replacement.
    STAGED=$(mktemp -d "$THEME_DIR/.luxe-theme-XXXXXX") || {
        report_failure "$NAME: cannot create staging directory"
        continue
    }
    if ! timeout 30s cp -R "$dir/." "$STAGED/"; then
        rm -rf "$STAGED"; STAGED=
        report_failure "$NAME: copy failed"
        continue
    fi
    if ! "$PUBLISH_HELPER" "$STAGED" "$THEME_DIR/$NAME"; then
        rm -rf "$STAGED"; STAGED=
        report_failure "$NAME: could not publish; existing destination was not removed"
        continue
    fi
    STAGED=
    rm -rf "$dir"
    installed=1
    echo "$NAME"
done

if [ "$installed" -eq 0 ]; then
    echo "$ARCHIVE: no installable icon theme found" >&2
    exit 1
fi
exit 0
