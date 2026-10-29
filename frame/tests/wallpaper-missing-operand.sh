#!/bin/sh
set -eu
frame=$1
fixture=$(mktemp -d)
trap 'rm -rf "$fixture"' EXIT HUP INT TERM
for option in --set-wallpaper --wallpaper-directory; do
    status=0
    DISPLAY= DBUS_SESSION_BUS_ADDRESS= "$frame" "$option" > "$fixture/out" 2> "$fixture/err" || status=$?
    test "$status" -eq 2
    test ! -s "$fixture/out"
    grep -Fx -- "$option requires a path" "$fixture/err" > /dev/null
done
