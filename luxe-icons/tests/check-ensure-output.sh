#!/bin/sh
set -eu
source_dir=$1
cmake=$2
tmp=$(mktemp -d "${TMPDIR:-/tmp}/luxe-icons-ensure-XXXXXX")
trap 'rm -rf -- "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp/source/cmake" "$tmp/build"
cat > "$tmp/source/cmake/BuildIcons.cmake" <<'EOF'
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
file(WRITE "${OUTPUT_DIR}/.luxe-icons-generated" "luxe-icons-generated-v1\n")
file(WRITE "${OUTPUT_DIR}/.built" "generated")
EOF
ensure() {
  "$cmake" -DSOURCE_DIR="$tmp/source" -DBUILD_ROOT="$tmp/build" \
    -DOUTPUT_DIR="$tmp/build/theme" -P "$source_dir/cmake/EnsureIcons.cmake"
}
ensure
test "$(cat "$tmp/build/theme/.built")" = generated
ensure
rm "$tmp/build/theme/.luxe-icons-generated"
if ensure >"$tmp/error" 2>&1; then
  echo 'unowned output was accepted' >&2
  exit 1
fi
test "$(cat "$tmp/build/theme/.built")" = generated
