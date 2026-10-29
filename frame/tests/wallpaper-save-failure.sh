#!/bin/sh
set -eu
frame=$1
fixture=$(mktemp -d)
pid=
cleanup() {
    if [ -n "$pid" ]; then kill "$pid" 2>/dev/null || :; wait "$pid" 2>/dev/null || :; fi
    rm -rf "$fixture"
}
trap cleanup EXIT HUP INT TERM
export XDG_CONFIG_HOME="$fixture/config" XDG_CACHE_HOME="$fixture/cache"
mkdir -p "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME"
printf 'P6\n1 1\n255\n\377\000\000' > "$fixture/image.ppm"
"$frame" --set-wallpaper "$fixture/image.ppm" > "$fixture/daemon.out" 2> "$fixture/daemon.err" &
pid=$!
attempt=0
while [ ! -f "$XDG_CONFIG_HOME/luxe/wallpaper.ini" ]; do
    if ! kill -0 "$pid" 2>/dev/null; then cat "$fixture/daemon.err" >&2; exit 1; fi
    attempt=$((attempt+1))
    if [ "$attempt" -ge 100 ]; then echo 'wallpaper service did not start' >&2; exit 1; fi
    sleep .05
done
mkdir "$fixture/empty"
status=0
"$frame" --wallpaper-directory "$fixture/empty" > "$fixture/empty.out" 2> "$fixture/empty.err" || status=$?
test "$status" -eq 1
grep -F 'Wallpaper source contains no usable image files' "$fixture/empty.err" > /dev/null
grep -F "source=$fixture/image.ppm" "$XDG_CONFIG_HOME/luxe/wallpaper.ini" > /dev/null
rm "$XDG_CONFIG_HOME/luxe/wallpaper.ini"
mkdir "$XDG_CONFIG_HOME/luxe/wallpaper.ini"
xprop -root _XROOTPMAP_ID > "$fixture/pixmap.before"
status=0
"$frame" --set-wallpaper "$fixture/image.ppm" > "$fixture/out" 2> "$fixture/err" || status=$?
test "$status" -eq 1
grep -F 'Wallpaper was not changed because its settings could not be saved:' "$fixture/err" > /dev/null
xprop -root _XROOTPMAP_ID > "$fixture/pixmap.after"
cmp -s "$fixture/pixmap.before" "$fixture/pixmap.after"
kill -0 "$pid"
"$frame" --wallpaper-stop
wait "$pid"
pid=
