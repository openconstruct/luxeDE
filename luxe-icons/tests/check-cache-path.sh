#!/bin/sh
set -eu
source_dir=$1
cmake=$2
tmp=$(mktemp -d "${TMPDIR:-/tmp}/luxe-icons-cache-XXXXXX")
trap 'rm -rf -- "$tmp"' EXIT HUP INT TERM
prefix="$tmp/prefix with spaces \$ ' \" \`"
"$cmake" -S "$source_dir" -B "$tmp/build" -DBUILD_TESTING=OFF "-DCMAKE_INSTALL_PREFIX=$prefix" >/dev/null
"$cmake" -S "$source_dir" -B "$tmp/usr-build" -DBUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX=/usr >/dev/null
grep -F 'icon_dir="/usr/share/icons"' "$tmp/usr-build/postinst" >/dev/null
mkdir -p "$tmp/bin" "$prefix/share/icons/Luxe"
: > "$prefix/share/icons/Luxe/index.theme"
cat > "$tmp/bin/gtk-update-icon-cache" <<'EOF'
#!/bin/sh
printf '%s\n' "$4" >> "$CACHE_TEST_LOG"
EOF
chmod +x "$tmp/bin/gtk-update-icon-cache"
export CACHE_TEST_LOG="$tmp/calls"
PATH="$tmp/bin:$PATH" sh "$tmp/build/postinst"
PATH="$tmp/bin:$PATH" sh "$tmp/build/postrm"
printf '%s\n%s\n' "$prefix/share/icons/Luxe" "$prefix/share/icons/Luxe" > "$tmp/expected"
cmp "$tmp/expected" "$CACHE_TEST_LOG"
