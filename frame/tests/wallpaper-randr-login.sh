#!/bin/sh
# A startup screen notification must not cancel login wallpaper rotation.
set -eu
frame=$1
gate=$2
randr=$3
fixture=$(mktemp -d)
pid=
cleanup() {
    : > "$fixture/release"
    if [ -n "$pid" ]; then kill "$pid" 2>/dev/null || :; wait "$pid" 2>/dev/null || :; fi
    rm -rf "$fixture"
}
trap cleanup EXIT HUP INT TERM
export XDG_CONFIG_HOME="$fixture/config" XDG_CACHE_HOME="$fixture/cache"
mkdir -p "$XDG_CONFIG_HOME/luxe" "$XDG_CACHE_HOME" "$fixture/images"
printf 'P6\n1 1\n255\n\377\000\000' > "$fixture/images/a.png"
printf 'P6\n1 1\n255\n\000\377\000' > "$fixture/images/b.png"
printf '[Wallpaper]\nsource=%s\ncurrent=%s\n' "$fixture/images" "$fixture/images/a.png" > "$XDG_CONFIG_HOME/luxe/wallpaper.ini"
FRAME_TEST_GATE_READY="$fixture/ready" FRAME_TEST_GATE_RELEASE="$fixture/release" \
    FRAME_TEST_RANDR_INITIAL=1 LD_PRELOAD="$gate:$randr" \
    "$frame" --wallpaper-daemon >"$fixture/out" 2>"$fixture/err" &
pid=$!
for attempt in $(seq 1 200); do
    [ -f "$fixture/ready" ] && break
    if ! kill -0 "$pid" 2>/dev/null; then cat "$fixture/err" >&2; exit 1; fi
    sleep .05
done
test -f "$fixture/ready"
# The pending X event is dispatched while the worker is held at the gate.
sleep 1
: > "$fixture/release"
for attempt in $(seq 1 200); do
    grep -Fx "current=$fixture/images/b.png" "$XDG_CONFIG_HOME/luxe/wallpaper.ini" >/dev/null && break
    if ! kill -0 "$pid" 2>/dev/null; then cat "$fixture/err" >&2; exit 1; fi
    sleep .05
done
if ! grep -Fx "current=$fixture/images/b.png" "$XDG_CONFIG_HOME/luxe/wallpaper.ini" >/dev/null; then
    cat "$fixture/err" "$XDG_CONFIG_HOME/luxe/wallpaper.ini" >&2
    exit 1
fi
"$frame" --wallpaper-stop
wait "$pid"
pid=
