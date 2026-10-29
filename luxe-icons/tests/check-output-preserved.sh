#!/bin/sh
set -eu
source_dir=$1
cmake=$2
tmp=$(mktemp -d "${TMPDIR:-/tmp}/luxe-icons-output-XXXXXX")
trap 'rm -rf -- "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp/source" "$tmp/build/theme"
printf '%s\n' 'invalid archive data' > "$tmp/source/Papirus.tar.gz"
printf '%s\n' 'unused' > "$tmp/source/Numix-circle.tar.gz"
papirus_hash=$(sha256sum "$tmp/source/Papirus.tar.gz" | cut -d ' ' -f 1)
numix_hash=$(sha256sum "$tmp/source/Numix-circle.tar.gz" | cut -d ' ' -f 1)
cat > "$tmp/source/sources.lock.json" <<EOF
[{"name":"Papirus","archive":"Papirus.tar.gz","sha256":"$papirus_hash"},{"name":"Numix-circle","archive":"Numix-circle.tar.gz","sha256":"$numix_hash"}]
EOF
printf '%s\n' 'luxe-icons-generated-v1' > "$tmp/build/theme/.luxe-icons-generated"
printf '%s\n' 'last known good theme' > "$tmp/build/theme/keep-me"
if "$cmake" -DSOURCE_DIR="$tmp/source" -DBUILD_ROOT="$tmp/build" \
    -DOUTPUT_DIR="$tmp/build/theme" -DICON_DEDUPLICATOR=/bin/true \
    -P "$source_dir/cmake/BuildIcons.cmake" \
    >"$tmp/build.log" 2>&1; then
  echo 'invalid archive unexpectedly generated icons' >&2
  exit 1
fi
test "$(cat "$tmp/build/theme/keep-me")" = 'last known good theme'
test "$(cat "$tmp/build/theme/.luxe-icons-generated")" = 'luxe-icons-generated-v1'
