#!/bin/sh
set -eu
broker=$1
renderer=$2
tmp=$(mktemp -d "${TMPDIR:-/tmp}/luxe-notify-timeout-XXXXXX")
trap 'kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; rm -rf "$tmp"' EXIT HUP INT TERM
export LUXE_NOTIFY_TEST_MARKER="$tmp/marker"
export LUXE_NOTIFY_TEST_STARTS="$tmp/starts"
"$broker" --renderer "$renderer" >"$tmp/log" 2>&1 &
pid=$!
owner() { gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus --method org.freedesktop.DBus.GetNameOwner org.freedesktop.Notifications; }
i=0
until owner >"$tmp/owner" 2>/dev/null; do
    i=$((i+1)); [ "$i" -lt 100 ] || { cat "$tmp/log"; exit 1; }
    sleep 0.05
done
notify() {
    gdbus call --session --dest org.freedesktop.Notifications --object-path /org/freedesktop/Notifications \
      --method org.freedesktop.Notifications.Notify test 0 '' test body '[]' '{}' 1000
}
if notify >"$tmp/first" 2>&1; then echo 'Hung request unexpectedly succeeded'; exit 1; fi
notify >"$tmp/second"
grep -F 'uint32 42' "$tmp/second" >/dev/null
if gdbus call --session --dest org.freedesktop.Notifications --object-path /org/freedesktop/Notifications \
    --method org.freedesktop.Notifications.Notify test 0 '' reject body '[]' '{}' 1000 >"$tmp/reject" 2>&1; then
    echo 'Method rejection unexpectedly succeeded'; exit 1
fi
grep -F 'org.luxe.Test.Rejected' "$tmp/reject" >/dev/null
notify >"$tmp/third"
grep -F 'uint32 42' "$tmp/third" >/dev/null
[ "$(wc -l < "$tmp/starts")" -eq 2 ]
owner >"$tmp/owner-after"
cmp "$tmp/owner" "$tmp/owner-after"
