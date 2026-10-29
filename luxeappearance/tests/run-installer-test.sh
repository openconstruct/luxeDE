#!/bin/bash
# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later
# Hermetic behavior tests for the icon-theme archive installer.
set -euo pipefail

installer=$1
publisher=$2
extractor=$3
if [ ! -x "$installer" ]; then
    echo "installer is not executable: $installer" >&2
    exit 2
fi

testdir=$(mktemp -d "${TMPDIR:-/tmp}/luxeappearance-installer-XXXXXX")
trap 'rm -rf "$testdir"' EXIT
mkdir -p "$testdir/tools"
cp "$installer" "$testdir/tools/install-icon-theme.sh"
cp "$publisher" "$testdir/tools/luxe-theme-publish"
cp "$extractor" "$testdir/tools/luxe-theme-extract"
installer="$testdir/tools/install-icon-theme.sh"
mkdir -p "$testdir/home" "$testdir/data" "$testdir/src/ThemeA/cursors"
printf '[Icon Theme]\nName=Theme A\nDirectories=cursors\n' > "$testdir/src/ThemeA/index.theme"
printf 'cursor fixture\n' > "$testdir/src/ThemeA/cursors/default"
printf 'binary fixture\n' > "$testdir/src/ThemeA/icon.png"
tar -czf "$testdir/theme.tar.gz" -C "$testdir/src" ThemeA

run_installer() {
    HOME="$testdir/home" XDG_DATA_HOME="$testdir/data" \
        "$installer" "$1"
}

output=$(run_installer "$testdir/theme.tar.gz")
test "$output" = "ThemeA"
test -f "$testdir/data/icons/ThemeA/index.theme"
test -f "$testdir/data/icons/ThemeA/icon.png"

# The alternate supported compression format goes through the same parser.
tar -cjf "$testdir/theme.tar.bz2" -C "$testdir/src" ThemeA
mkdir "$testdir/bzip-data"
HOME="$testdir/home" XDG_DATA_HOME="$testdir/bzip-data" \
    "$installer" "$testdir/theme.tar.bz2" > "$testdir/bzip.out"
test "$(cat "$testdir/bzip.out")" = ThemeA
test -f "$testdir/bzip-data/icons/ThemeA/index.theme"

# A duplicate is refused and the existing theme is not replaced.
printf 'changed\n' > "$testdir/data/icons/ThemeA/icon.png"
if run_installer "$testdir/theme.tar.gz" >"$testdir/duplicate.out" 2>"$testdir/duplicate.err"; then
    echo "duplicate archive unexpectedly succeeded" >&2
    exit 1
fi
test "$(cat "$testdir/data/icons/ThemeA/icon.png")" = "changed"
grep -q 'already exists' "$testdir/duplicate.err"

# Two same-name installs race at publication. Exactly one wins and the loser
# must never remove or merge into the winner's complete theme.
mkdir "$testdir/concurrent-basic-data"
HOME="$testdir/home" XDG_DATA_HOME="$testdir/concurrent-basic-data" \
    "$installer" "$testdir/theme.tar.gz" >"$testdir/concurrent-a.out" 2>"$testdir/concurrent-a.err" &
first=$!
HOME="$testdir/home" XDG_DATA_HOME="$testdir/concurrent-basic-data" \
    "$installer" "$testdir/theme.tar.gz" >"$testdir/concurrent-b.out" 2>"$testdir/concurrent-b.err" &
second=$!
first_status=0; wait "$first" || first_status=$?
second_status=0; wait "$second" || second_status=$?
test "$(( (first_status == 0) + (second_status == 0) ))" -eq 1
test -f "$testdir/concurrent-basic-data/icons/ThemeA/index.theme"
test -f "$testdir/concurrent-basic-data/icons/ThemeA/icon.png"
if find "$testdir/concurrent-basic-data/icons" -maxdepth 1 -name '.luxe-theme-*' | grep -q .; then
    echo 'failed concurrent publish left staging behind' >&2
    exit 1
fi

# A traversal member is rejected by the same parser that extracts the archive.
printf 'owned\n' > "$testdir/payload"
tar -czf "$testdir/traversal.tar.gz" -C "$testdir" \
    --transform='s|^payload$|../escape|' payload
if run_installer "$testdir/traversal.tar.gz" >"$testdir/traversal.out" 2>"$testdir/traversal.err"; then
    echo "traversal archive unexpectedly succeeded" >&2
    exit 1
fi
grep -q 'Unsafe archive path' "$testdir/traversal.err"
test ! -e "$testdir/escape"

# Symlink and corrupt archives are rejected too.
ln -s /etc/passwd "$testdir/src/ThemeA/evil-link"
tar -czf "$testdir/link.tar.gz" -C "$testdir/src" ThemeA
if run_installer "$testdir/link.tar.gz" >"$testdir/link.out" 2>"$testdir/link.err"; then
    echo "symlink archive unexpectedly succeeded" >&2
    exit 1
fi
grep -q 'unsupported link or special member' "$testdir/link.err"

# Newline-bearing names are parsed as archive fields, never shell records.
mkdir -p "$testdir/newline-src/ThemeNewline"
printf '[Icon Theme]\nName=Theme Newline\nDirectories=\n' > "$testdir/newline-src/ThemeNewline/index.theme"
printf 'safe newline path\n' > "$testdir/newline-src/ThemeNewline/$'line\nbreak.txt'"
tar -czf "$testdir/newline.tar.gz" -C "$testdir/newline-src" ThemeNewline
mkdir "$testdir/newline-data"
HOME="$testdir/home" XDG_DATA_HOME="$testdir/newline-data" \
    "$installer" "$testdir/newline.tar.gz" > "$testdir/newline.out"
test "$(cat "$testdir/newline.out")" = ThemeNewline
test -f "$testdir/newline-data/icons/ThemeNewline/$'line\nbreak.txt'"

# Device nodes and FIFOs are rejected without creating filesystem objects.
mkdir "$testdir/special-src"
mkfifo "$testdir/special-src/pipe"
tar -czf "$testdir/special.tar.gz" -C "$testdir/special-src" pipe
mkdir "$testdir/special-data"
if HOME="$testdir/home" XDG_DATA_HOME="$testdir/special-data" \
    "$installer" "$testdir/special.tar.gz" > "$testdir/special.out" 2> "$testdir/special.err"; then
    echo 'archive with a FIFO member unexpectedly succeeded' >&2
    exit 1
fi
grep -q 'unsupported link or special member' "$testdir/special.err"
test ! -e "$testdir/special-data/icons/pipe"

# Device nodes are rejected too when the test environment permits creating
# them in its disposable archive fixture.
mkdir "$testdir/device-src"
if mknod "$testdir/device-src/device" c 1 3 2>/dev/null; then
    tar -czf "$testdir/device.tar.gz" -C "$testdir/device-src" device
    mkdir "$testdir/device-data"
    if HOME="$testdir/home" XDG_DATA_HOME="$testdir/device-data" \
        "$installer" "$testdir/device.tar.gz" > "$testdir/device.out" 2> "$testdir/device.err"; then
        echo 'archive with a device node unexpectedly succeeded' >&2
        exit 1
    fi
    grep -q 'unsupported link or special member' "$testdir/device.err"
    test ! -e "$testdir/device-data/icons/device"
fi

# The parser deliberately rejects PAX/GNU long-name metadata rather than
# trusting extensions that this small bounded ustar parser does not implement.
mkdir -p "$testdir/long-src/ThemeLong"
printf '[Icon Theme]\nName=Long\nDirectories=\n' > "$testdir/long-src/ThemeLong/index.theme"
long_name=$(printf '%0100d' 0).txt
printf 'long name fixture\n' > "$testdir/long-src/ThemeLong/$long_name"
tar -czf "$testdir/long.tar.gz" -C "$testdir/long-src" ThemeLong
mkdir "$testdir/long-data"
if HOME="$testdir/home" XDG_DATA_HOME="$testdir/long-data" \
    "$installer" "$testdir/long.tar.gz" > "$testdir/long.out" 2> "$testdir/long.err"; then
    echo 'PAX long-name archive unexpectedly succeeded' >&2
    exit 1
fi
grep -q 'PAX/GNU extended tar metadata is unsupported' "$testdir/long.err"
test ! -e "$testdir/long-data/icons/ThemeLong"

# Conflicting duplicate member names are rejected before publication.
tar -cf "$testdir/duplicate-member.tar" -C "$testdir/src" ThemeA/icon.png
tar -rf "$testdir/duplicate-member.tar" -C "$testdir/src" ThemeA/icon.png
gzip "$testdir/duplicate-member.tar"
mkdir "$testdir/duplicate-member"
if "$extractor" "$testdir/duplicate-member.tar.gz" "$testdir/duplicate-member" > "$testdir/duplicate-member.out" 2> "$testdir/duplicate-member.err"; then
    echo 'archive with a duplicate member unexpectedly succeeded' >&2
    exit 1
fi
grep -q 'Duplicate archive member' "$testdir/duplicate-member.err"

# An injected partial-copy failure cannot expose a final directory or leave
# either destination-side staging or private extraction artifacts behind.
mkdir "$testdir/failing-bin" "$testdir/failed-copy-data" "$testdir/private-tmp"
cat > "$testdir/failing-bin/cp" <<'STUB'
#!/bin/sh
printf 'partial\n' > "$3/interrupted"
exit 1
STUB
chmod +x "$testdir/failing-bin/cp"
if PATH="$testdir/failing-bin:$PATH" TMPDIR="$testdir/private-tmp" \
    HOME="$testdir/home" XDG_DATA_HOME="$testdir/failed-copy-data" \
    "$installer" "$testdir/theme.tar.gz" > "$testdir/copy-fail.out" 2> "$testdir/copy-fail.err"; then
    echo 'injected partial copy failure unexpectedly succeeded' >&2
    exit 1
fi
test ! -e "$testdir/failed-copy-data/icons/ThemeA"
if find "$testdir/failed-copy-data/icons" "$testdir/private-tmp" -mindepth 1 -maxdepth 2 | grep -q .; then
    echo 'partial copy failure left staging or extraction files behind' >&2
    exit 1
fi

# If the bounded extractor reports a timeout, the shell trap still removes its
# private extraction tree and publishes nothing.
mkdir "$testdir/timeout-bin" "$testdir/timeout-data" "$testdir/timeout-tmp"
cat > "$testdir/timeout-bin/timeout" <<'STUB'
#!/bin/sh
exit 124
STUB
chmod +x "$testdir/timeout-bin/timeout"
if PATH="$testdir/timeout-bin:$PATH" TMPDIR="$testdir/timeout-tmp" \
    HOME="$testdir/home" XDG_DATA_HOME="$testdir/timeout-data" \
    "$installer" "$testdir/theme.tar.gz" > "$testdir/timeout.out" 2> "$testdir/timeout.err"; then
    echo 'injected extractor timeout unexpectedly succeeded' >&2
    exit 1
fi
test ! -e "$testdir/timeout-data/icons/ThemeA"
if find "$testdir/timeout-tmp" -mindepth 1 -maxdepth 1 | grep -q .; then
    echo 'extractor timeout left private data behind' >&2
    exit 1
fi

# Swap the selected path to a symlink after the shell's initial checks. The
# extractor opens with O_NOFOLLOW and rejects the replacement.
cp "$testdir/theme.tar.gz" "$testdir/symlink-race.tar.gz"
mv "$testdir/tools/luxe-theme-extract" "$testdir/tools/real-extract"
cat > "$testdir/tools/luxe-theme-extract" <<'STUB'
#!/bin/sh
mv "$1" "$1.original"
ln -s "$1.original" "$1"
exec "$(dirname "$0")/real-extract" "$@"
STUB
chmod +x "$testdir/tools/luxe-theme-extract"
mkdir "$testdir/symlink-race-data"
if HOME="$testdir/home" XDG_DATA_HOME="$testdir/symlink-race-data" \
    "$installer" "$testdir/symlink-race.tar.gz" > "$testdir/symlink-race.out" 2> "$testdir/symlink-race.err"; then
    echo 'archive symlink replacement unexpectedly succeeded' >&2
    exit 1
fi
test ! -e "$testdir/symlink-race-data/icons/ThemeA"
grep -q 'without following links' "$testdir/symlink-race.err"
mv "$testdir/tools/real-extract" "$testdir/tools/luxe-theme-extract"
truncate -s 134217729 "$testdir/oversized.tar.gz"
if HOME="$testdir/home" XDG_DATA_HOME="$testdir/symlink-race-data" \
    "$installer" "$testdir/oversized.tar.gz" > "$testdir/oversized.out" 2> "$testdir/oversized.err"; then
    echo 'oversized archive unexpectedly succeeded' >&2
    exit 1
fi
grep -q 'exceeds compressed size limit' "$testdir/oversized.err"

# Entry count is bounded independently of compressed archive size.
mkdir "$testdir/many"
for ((i=0; i<4097; ++i)); do : > "$testdir/many/f$i"; done
tar -czf "$testdir/many.tar.gz" -C "$testdir/many" .
if run_installer "$testdir/many.tar.gz" > "$testdir/many.out" 2> "$testdir/many.err"; then
    echo 'archive with too many members unexpectedly succeeded' >&2
    exit 1
fi
grep -q 'too many entries' "$testdir/many.err"

# A tiny compressed archive with a huge declared file size fails before data
# extraction, proving the expanded-byte cap is enforced from tar headers.
mkdir "$testdir/huge"
truncate -s 268435457 "$testdir/huge/payload"
tar -czf "$testdir/huge.tar.gz" -C "$testdir/huge" payload
if run_installer "$testdir/huge.tar.gz" > "$testdir/huge.out" 2> "$testdir/huge.err"; then
    echo 'archive over the expanded-byte limit unexpectedly succeeded' >&2
    exit 1
fi
grep -q 'expanded size limit' "$testdir/huge.err"

# A competing destination created just before publication is never removed.
mkdir -p "$testdir/publish-race-data"
mv "$testdir/tools/luxe-theme-publish" "$testdir/tools/real-publish"
cat > "$testdir/tools/luxe-theme-publish" <<'STUB'
#!/bin/sh
mkdir "$2"
printf 'other installer\n' > "$2/sentinel"
exec "$(dirname "$0")/real-publish" "$@"
STUB
chmod +x "$testdir/tools/luxe-theme-publish"
if HOME="$testdir/home" XDG_DATA_HOME="$testdir/publish-race-data" \
    "$installer" "$testdir/theme.tar.gz" > "$testdir/publish-race.out" 2> "$testdir/publish-race.err"; then
    echo 'competing publication unexpectedly succeeded' >&2
    exit 1
fi
test "$(cat "$testdir/publish-race-data/icons/ThemeA/sentinel")" = 'other installer'
test "$(find "$testdir/publish-race-data/icons" -maxdepth 1 -name '.luxe-theme-*' | wc -l)" -eq 0
mv "$testdir/tools/real-publish" "$testdir/tools/luxe-theme-publish"

# Two installers both reach publication before either can rename its staging.
mkdir -p "$testdir/concurrent-data" "$testdir/publish-barrier"
mv "$testdir/tools/luxe-theme-publish" "$testdir/tools/real-publish"
cat > "$testdir/tools/luxe-theme-publish" <<'STUB'
#!/bin/sh
touch "$BARRIER_DIR/$$"
count=0
while [ "$(find "$BARRIER_DIR" -type f | wc -l)" -lt 2 ]; do
    count=$((count+1)); [ "$count" -lt 500 ] || exit 3
    sleep .01
done
exec "$(dirname "$0")/real-publish" "$@"
STUB
chmod +x "$testdir/tools/luxe-theme-publish"
HOME="$testdir/home" XDG_DATA_HOME="$testdir/concurrent-data" BARRIER_DIR="$testdir/publish-barrier" \
    "$installer" "$testdir/theme.tar.gz" > "$testdir/concurrent-1.out" 2> "$testdir/concurrent-1.err" & p1=$!
HOME="$testdir/home" XDG_DATA_HOME="$testdir/concurrent-data" BARRIER_DIR="$testdir/publish-barrier" \
    "$installer" "$testdir/theme.tar.gz" > "$testdir/concurrent-2.out" 2> "$testdir/concurrent-2.err" & p2=$!
status1=0;status2=0
wait "$p1" || status1=$?
wait "$p2" || status2=$?
test $(( (status1 == 0) + (status2 == 0) )) -eq 1
test -f "$testdir/concurrent-data/icons/ThemeA/index.theme"
test -f "$testdir/concurrent-data/icons/ThemeA/icon.png"
test "$(find "$testdir/concurrent-data/icons" -maxdepth 1 -name '.luxe-theme-*' | wc -l)" -eq 0
mv "$testdir/tools/real-publish" "$testdir/tools/luxe-theme-publish"

printf 'not a tar archive\n' > "$testdir/corrupt.tar.gz"
if run_installer "$testdir/corrupt.tar.gz" >"$testdir/corrupt.out" 2>"$testdir/corrupt.err"; then
    echo "corrupt archive unexpectedly succeeded" >&2
    exit 1
fi
grep -q 'archive validation or extraction failed' "$testdir/corrupt.err"

# A valid tar with a damaged gzip trailer must fail checksum validation.
cp "$testdir/theme.tar.gz" "$testdir/bad-checksum.tar.gz"
size=$(stat -c %s "$testdir/bad-checksum.tar.gz")
printf '\377' | dd of="$testdir/bad-checksum.tar.gz" bs=1 seek=$((size-1)) conv=notrunc status=none
if run_installer "$testdir/bad-checksum.tar.gz" > "$testdir/bad-checksum.out" 2> "$testdir/bad-checksum.err"; then
    echo 'archive with a damaged gzip checksum unexpectedly succeeded' >&2
    exit 1
fi
grep -q 'Invalid gzip archive' "$testdir/bad-checksum.err"

# Relative XDG_DATA_HOME is ignored and falls back to HOME.
rm -rf "$testdir/relative-out" "$testdir/home/.local"
mkdir -p "$testdir/relative-out"
(
    cd "$testdir/relative-out"
    HOME="$testdir/home" XDG_DATA_HOME=relative \
        "$installer" "$testdir/theme.tar.gz" >/dev/null
)
test -f "$testdir/home/.local/share/icons/ThemeA/index.theme"
test ! -e "$testdir/relative-out/relative/icons/ThemeA"

printf '%s\n' 'Installer behavior tests passed.'
