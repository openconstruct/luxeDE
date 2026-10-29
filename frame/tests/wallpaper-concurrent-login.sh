#!/bin/sh
# Reproduce the session and PCManager starting Frame at the same time.
set -eu
frame=$1
gate=$2
randr=$3
fixture=$(mktemp -d)
pid=
second=
cleanup() {
    : > "$fixture/release"
    if [ -n "$second" ]; then kill "$second" 2>/dev/null || :; wait "$second" 2>/dev/null || :; fi
    if [ -n "$pid" ]; then kill "$pid" 2>/dev/null || :; wait "$pid" 2>/dev/null || :; fi
    rm -rf "$fixture"
}
trap cleanup EXIT HUP INT TERM
export XDG_CONFIG_HOME="$fixture/config" XDG_CACHE_HOME="$fixture/cache"
mkdir -p "$XDG_CONFIG_HOME/luxe" "$XDG_CACHE_HOME" "$fixture/images"
# GdkPixbuf detects the PPM contents even though directory scanning uses extensions.
printf 'P6\n1 1\n255\n\377\000\000' > "$fixture/images/a.png"
printf 'P6\n1 1\n255\n\000\377\000' > "$fixture/images/b.png"
printf '[Wallpaper]\nsource=%s\ncurrent=%s\n' "$fixture/images" "$fixture/images/a.png" > "$XDG_CONFIG_HOME/luxe/wallpaper.ini"
FRAME_TEST_GATE_READY="$fixture/ready" FRAME_TEST_GATE_RELEASE="$fixture/release" \
    FRAME_TEST_RANDR_INITIAL=1 LD_PRELOAD="$gate:$randr" \
    "$frame" --wallpaper-daemon >"$fixture/first.out" 2>"$fixture/first.err" &
pid=$!
for attempt in $(seq 1 200); do
    [ -f "$fixture/ready" ] && break
    if ! kill -0 "$pid" 2>/dev/null; then cat "$fixture/first.err" >&2; exit 1; fi
    sleep .05
done
test -f "$fixture/ready"
"$frame" --wallpaper-daemon >"$fixture/second.out" 2>"$fixture/second.err" &
second=$!
# The corrected command returns immediately; the old one waits on the gated
# decode after cancelling it. Give either enough time to reach the service.
for attempt in $(seq 1 40); do
    kill -0 "$second" 2>/dev/null || break
    sleep .05
done
: > "$fixture/release"
if ! wait "$second"; then cat "$fixture/second.err" >&2; exit 1; fi
second=
for attempt in $(seq 1 200); do
    grep -Fx "current=$fixture/images/b.png" "$XDG_CONFIG_HOME/luxe/wallpaper.ini" >/dev/null && break
    if ! kill -0 "$pid" 2>/dev/null; then cat "$fixture/first.err" >&2; exit 1; fi
    sleep .05
done
if ! grep -Fx "current=$fixture/images/b.png" "$XDG_CONFIG_HOME/luxe/wallpaper.ini" >/dev/null; then
    cat "$fixture/first.err" "$fixture/second.err" "$XDG_CONFIG_HOME/luxe/wallpaper.ini" >&2
    exit 1
fi
"$frame" --wallpaper-stop
wait "$pid"
pid=

# Also ensure a duplicate activation cannot reload old settings while an
# explicit choice is being prepared and commit the new image under the old source.
rm "$fixture/ready" "$fixture/release"
FRAME_TEST_GATE_READY="$fixture/ready" FRAME_TEST_GATE_RELEASE="$fixture/release" \
    FRAME_TEST_RANDR_INITIAL=1 LD_PRELOAD="$gate:$randr" \
    "$frame" --set-wallpaper "$fixture/images/a.png" >"$fixture/first.out" 2>"$fixture/first.err" &
pid=$!
for attempt in $(seq 1 200); do
    [ -f "$fixture/ready" ] && break
    if ! kill -0 "$pid" 2>/dev/null; then cat "$fixture/first.err" >&2; exit 1; fi
    sleep .05
done
test -f "$fixture/ready"
"$frame" --wallpaper-daemon >"$fixture/second.out" 2>"$fixture/second.err" &
second=$!
for attempt in $(seq 1 40); do
    kill -0 "$second" 2>/dev/null || break
    sleep .05
done
: > "$fixture/release"
if ! wait "$second"; then cat "$fixture/second.err" >&2; exit 1; fi
second=
for attempt in $(seq 1 200); do
    grep -Fx "source=$fixture/images/a.png" "$XDG_CONFIG_HOME/luxe/wallpaper.ini" >/dev/null && break
    if ! kill -0 "$pid" 2>/dev/null; then cat "$fixture/first.err" >&2; exit 1; fi
    sleep .05
done
grep -Fx "source=$fixture/images/a.png" "$XDG_CONFIG_HOME/luxe/wallpaper.ini" >/dev/null
grep -Fx "current=$fixture/images/a.png" "$XDG_CONFIG_HOME/luxe/wallpaper.ini" >/dev/null
"$frame" --wallpaper-stop
wait "$pid"
pid=
