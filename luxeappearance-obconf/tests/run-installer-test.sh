#!/bin/bash
# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
# Hermetic tests for the Openbox theme archive installer.
set -euo pipefail

installer=$1
extractor=$2
publisher=$3
if [ ! -x "$installer" ]; then
    echo "installer is not executable: $installer" >&2
    exit 2
fi
if [ ! -x "$extractor" ] || [ ! -x "$publisher" ]; then
    echo "theme helper is not executable" >&2
    exit 2
fi
export OBCONF_THEME_EXTRACTOR="$extractor"
export OBCONF_THEME_PUBLISHER="$publisher"

testdir=$(mktemp -d "${TMPDIR:-/tmp}/obconf-installer-XXXXXX")
trap 'rm -rf "$testdir"' EXIT
export HOME="$testdir/home"
mkdir -p "$HOME" "$testdir/src/Theme/openbox-3" "$testdir/src/Other/openbox-3"
printf '[Desktop]\n' > "$testdir/src/Theme/openbox-3/themerc"
printf 'window: { title: OK; }\n' > "$testdir/src/Theme/openbox-3/theme.conf"
printf '[Desktop]\n' > "$testdir/src/Other/openbox-3/themerc"
tar -czf "$testdir/theme.obt" -C "$testdir/src" Theme

output=$(HOME="$HOME" "$installer" "$testdir/theme.obt")
test "$output" = "Theme"
test -f "$HOME/.themes/Theme/openbox-3/themerc"

# Existing themes are not silently replaced.
printf 'changed\n' > "$HOME/.themes/Theme/openbox-3/themerc"
if HOME="$HOME" "$installer" "$testdir/theme.obt" >"$testdir/duplicate.out" 2>"$testdir/duplicate.err"; then
    echo "duplicate archive unexpectedly succeeded" >&2
    exit 1
fi
test "$(cat "$HOME/.themes/Theme/openbox-3/themerc")" = "changed"
grep -q 'already exists' "$testdir/duplicate.err"

# More than one top-level theme is ambiguous and rejected.
tar -czf "$testdir/two.obt" -C "$testdir/src" Theme Other
if HOME="$HOME" "$installer" "$testdir/two.obt" >"$testdir/two.out" 2>"$testdir/two.err"; then
    echo "multi-theme archive unexpectedly succeeded" >&2
    exit 1
fi
grep -q 'more than one' "$testdir/two.err"

# A symbolic-link ~/.themes is not followed by the installer.
mkdir -p "$testdir/symlink-home" "$testdir/outside"
ln -s "$testdir/outside" "$testdir/symlink-home/.themes"
if HOME="$testdir/symlink-home" "$installer" "$testdir/theme.obt" >"$testdir/symlink.out" 2>"$testdir/symlink.err"; then
    echo "symlink theme root unexpectedly succeeded" >&2
    exit 1
fi
grep -q 'unsafe theme directory' "$testdir/symlink.err"
test -z "$(find "$testdir/outside" -mindepth 1 -print -quit)"

# Traversal, link, and corrupt members are rejected before extraction.
printf 'bad\n' > "$testdir/payload"
tar -czf "$testdir/traversal.obt" -C "$testdir" \
    --transform='s|^payload$|../escape|' payload
tar -czf "$testdir/absolute.obt" -C "$testdir" \
    --transform='s|^payload$|/tmp/obconf-absolute-escape|' payload
mkdir -p "$testdir/members/Theme/openbox-3"
ln -s /etc/passwd "$testdir/members/Theme/openbox-3/link"
mkfifo "$testdir/members/Theme/openbox-3/fifo"
printf 'fixture\n' > "$testdir/members/Theme/openbox-3/themerc"
ln "$testdir/members/Theme/openbox-3/themerc" "$testdir/members/Theme/openbox-3/hardlink"
tar -czf "$testdir/link.obt" -C "$testdir/members" Theme/openbox-3/link
tar -czf "$testdir/fifo.obt" -C "$testdir/members" Theme/openbox-3/fifo
tar -czf "$testdir/hardlink.obt" -C "$testdir/members" \
    Theme/openbox-3/themerc Theme/openbox-3/hardlink
printf 'not a tar archive\n' > "$testdir/corrupt.obt"
for archive in traversal.obt link.obt hardlink.obt fifo.obt absolute.obt corrupt.obt; do
    if HOME="$HOME" "$installer" "$testdir/$archive" >"$testdir/$archive.out" 2>"$testdir/$archive.err"; then
        echo "$archive unexpectedly succeeded" >&2
        exit 1
    fi
done
test ! -e "$testdir/escape"
grep -q 'Unsafe archive path' "$testdir/traversal.obt.err"
grep -q 'unsupported link or special' "$testdir/link.obt.err"
grep -q 'unsupported link or special' "$testdir/hardlink.obt.err"
grep -q 'unsupported link or special' "$testdir/fifo.obt.err"
grep -q 'Unsafe archive path' "$testdir/absolute.obt.err"
grep -q 'bounded theme extraction failed' "$testdir/corrupt.obt.err"

# Entry count is enforced by the shared parser while it reads headers.
mkdir -p "$testdir/many"
for i in $(seq 1 4100); do : > "$testdir/many/file-$i"; done
tar -czf "$testdir/many.obt" -C "$testdir/many" .
if HOME="$HOME" "$installer" "$testdir/many.obt" >"$testdir/many.out" 2>"$testdir/many.err"; then
    echo "oversized entry-count archive unexpectedly succeeded" >&2
    exit 1
fi
grep -q 'too many entries' "$testdir/many.err"

# A tiny gzip bomb carrying one file beyond the aggregate expanded-byte limit
# is rejected before its payload is written to the extraction directory.
mkdir -p "$testdir/bomb/Theme/openbox-3"
printf '[Desktop]\n' > "$testdir/bomb/Theme/openbox-3/themerc"
truncate -s 268435457 "$testdir/bomb/Theme/payload"
tar -czf "$testdir/bomb.obt" -C "$testdir/bomb" Theme
if HOME="$HOME" "$installer" "$testdir/bomb.obt" >"$testdir/bomb.out" 2>"$testdir/bomb.err"; then
    echo "expanded-size bomb unexpectedly succeeded" >&2
    exit 1
fi
grep -q 'expanded size limit' "$testdir/bomb.err"

# Reject oversized compressed input before making a snapshot.
truncate -s 134217729 "$testdir/large.obt"
if HOME="$HOME" "$installer" "$testdir/large.obt" >"$testdir/large.out" 2>"$testdir/large.err"; then
    echo "oversized compressed archive unexpectedly succeeded" >&2
    exit 1
fi
grep -q 'compressed size limit' "$testdir/large.err"

# Staging-copy failure must not leave a partial visible theme.
mkdir -p "$testdir/failbin" "$testdir/fail-home"
cat > "$testdir/failbin/cp" <<'EOF'
#!/bin/sh
exit 1
EOF
chmod +x "$testdir/failbin/cp"
if HOME="$testdir/fail-home" PATH="$testdir/failbin:$PATH" "$installer" "$testdir/theme.obt" >"$testdir/copy.out" 2>"$testdir/copy.err"; then
    echo "injected staging failure unexpectedly succeeded" >&2
    exit 1
fi
test ! -e "$testdir/fail-home/.themes/Theme"
test -z "$(find "$testdir/fail-home/.themes" -maxdepth 1 -name '.obconf-theme-*' -print -quit 2>/dev/null || true)"

# Exactly one racing installer wins; the publisher never replaces the winner.
mkdir -p "$testdir/race-home"
HOME="$testdir/race-home" "$installer" "$testdir/theme.obt" >"$testdir/race1.out" 2>"$testdir/race1.err" & p1=$!
HOME="$testdir/race-home" "$installer" "$testdir/theme.obt" >"$testdir/race2.out" 2>"$testdir/race2.err" & p2=$!
set +e
wait "$p1"; s1=$?
wait "$p2"; s2=$?
set -e
if [ "$s1" -eq 0 ] && [ "$s2" -eq 0 ] || [ "$s1" -ne 0 ] && [ "$s2" -ne 0 ]; then
    echo "concurrent installer result was not exactly one success ($s1, $s2)" >&2
    exit 1
fi
test -f "$testdir/race-home/.themes/Theme/openbox-3/themerc"
test "$(cat "$testdir/race-home/.themes/Theme/openbox-3/themerc")" = '[Desktop]'
test -z "$(find "$testdir/race-home/.themes" -maxdepth 1 -name '.obconf-theme-*' -print -quit)"

printf '%s\n' 'ObConf installer behavior tests passed.'
