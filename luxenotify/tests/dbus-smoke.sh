#!/bin/sh
# Copyright (C) 2026 Jerry Howell
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu

if [ -z "${DISPLAY:-}" ]; then
    echo "SKIP: no X11 display and xvfb-run is unavailable"
    exit 77
fi

daemon=$1
"$daemon" &
daemon_pid=$!
trap 'kill "$daemon_pid" 2>/dev/null || true; wait "$daemon_pid" 2>/dev/null || true' EXIT INT TERM

# Wait until our daemon owns the bus name before sending any calls to it.
# Querying NameHasOwner does not trigger service activation, unlike a direct
# call, so an installed provider elsewhere on the session bus cannot win a
# startup race and answer on our daemon's behalf.
owner=false
attempt=0
while [ "$attempt" -lt 100 ]; do
    if gdbus call --session \
        --dest org.freedesktop.DBus \
        --object-path /org/freedesktop/DBus \
        --method org.freedesktop.DBus.NameHasOwner \
        "org.freedesktop.Notifications" 2>/dev/null | grep -q "true"; then
        owner=true
        break
    fi
    # If our daemon already exited (e.g. name taken), fail fast.
    if ! kill -0 "$daemon_pid" 2>/dev/null; then
        echo "LuxeNotify daemon exited before owning its D-Bus name" >&2
        exit 1
    fi
    attempt=$((attempt + 1))
    sleep 0.05
done
if [ "$owner" != true ]; then
    echo "LuxeNotify did not acquire its D-Bus service name" >&2
    exit 1
fi

ready=false
attempt=0
while [ "$attempt" -lt 50 ]; do
    if server_info=$(gdbus call --session \
        --dest org.freedesktop.Notifications \
        --object-path /org/freedesktop/Notifications \
        --method org.freedesktop.Notifications.GetServerInformation \
        2>/dev/null); then
        ready=true
        break
    fi
    attempt=$((attempt + 1))
    sleep 0.05
done
if [ "$ready" != true ]; then
    echo "LuxeNotify did not acquire its D-Bus service name" >&2
    exit 1
fi
printf '%s\n' "$server_info" | grep -q "LuxeNotify"

capabilities=$(gdbus call --session \
    --dest org.freedesktop.Notifications \
    --object-path /org/freedesktop/Notifications \
    --method org.freedesktop.Notifications.GetCapabilities)
printf '%s\n' "$capabilities" | grep -q "actions"
# body-hyperlinks is advertised because bodies are sanitized to a tag
# whitelist with http(s)/mailto-only hrefs; persistence is not advertised
# since notifications live only in memory for the session.
printf '%s\n' "$capabilities" | grep -q "body-hyperlinks"
if printf '%s\n' "$capabilities" | grep -q "persistence"; then
    echo "persistence capability must not be advertised" >&2
    exit 1
fi

notify_id() {
    printf '%s\n' "$1" | sed -n 's/.*uint32 \([0-9][0-9]*\).*/\1/p'
}

close_notification() {
    gdbus call --session \
        --dest org.freedesktop.Notifications \
        --object-path /org/freedesktop/Notifications \
        --method org.freedesktop.Notifications.CloseNotification "$1" >/dev/null
}

created=$(gdbus call --session \
    --dest org.freedesktop.Notifications \
    --object-path /org/freedesktop/Notifications \
    --method org.freedesktop.Notifications.Notify \
    "Luxe test" 0 "dialog-information" "Notification test" \
    "D-Bus and GTK4 window creation are working." \
    "['default', 'Open', 'dismiss', 'Dismiss']" \
    "{'urgency': <byte 1>, 'value': <int32 42>}" 250)
id=$(printf '%s\n' "$created" | sed -n 's/.*uint32 \([0-9][0-9]*\).*/\1/p')
test -n "$id"
active=$(gdbus call --session \
    --dest org.freedesktop.Notifications \
    --object-path /org/freedesktop/Notifications \
    --method org.luxe.Notifications.GetActiveCount)
printf '%s\n' "$active" | grep -Eq 'uint32 [1-9][0-9]*'

replaced=$(gdbus call --session \
    --dest org.freedesktop.Notifications \
    --object-path /org/freedesktop/Notifications \
    --method org.freedesktop.Notifications.Notify \
    "Luxe test" "$id" "dialog-information" "Replacement test" \
    "Replacement IDs preserve the notification identity." "[]" "{}" 250)
printf '%s\n' "$replaced" | grep -q "uint32 $id"

gdbus call --session \
    --dest org.freedesktop.Notifications \
    --object-path /org/freedesktop/Notifications \
    --method org.freedesktop.Notifications.CloseNotification "$id" >/dev/null
active=$(gdbus call --session \
    --dest org.freedesktop.Notifications \
    --object-path /org/freedesktop/Notifications \
    --method org.luxe.Notifications.GetActiveCount)
printf '%s\n' "$active" | grep -q 'uint32 0'

# Urgency styling paths: critical and low must each yield a notification.
critical=$(gdbus call --session \
    --dest org.freedesktop.Notifications \
    --object-path /org/freedesktop/Notifications \
    --method org.freedesktop.Notifications.Notify \
    "Luxe test" 0 "dialog-information" "Critical urgency" \
    "Critical styling path." "[]" "{'urgency': <byte 2>}" 500)
critical_id=$(notify_id "$critical")
test -n "$critical_id"

low=$(gdbus call --session \
    --dest org.freedesktop.Notifications \
    --object-path /org/freedesktop/Notifications \
    --method org.freedesktop.Notifications.Notify \
    "Luxe test" 0 "dialog-information" "Low urgency" \
    "Low styling path." "[]" "{'urgency': <byte 0>}" 500)
low_id=$(notify_id "$low")
test -n "$low_id"

# Inline image-data hint: a 2x2 RGBA image exercises the pixbuf path.
image_hint="{'image-data': <(2, 2, 8, true, 8, 4, [255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255])>}"
imaged=$(gdbus call --session \
    --dest org.freedesktop.Notifications \
    --object-path /org/freedesktop/Notifications \
    --method org.freedesktop.Notifications.Notify \
    "Luxe test" 0 "" "Image data" \
    "Inline image-data hint." "[]" "$image_hint" 500)
image_id=$(notify_id "$imaged")
test -n "$image_id"

# Unreadable image-path must fall back to the stock icon, not crash.
missing_image=$(gdbus call --session \
    --dest org.freedesktop.Notifications \
    --object-path /org/freedesktop/Notifications \
    --method org.freedesktop.Notifications.Notify \
    "Luxe test" 0 "" "Missing image" \
    "Unreadable image-path falls back." "[]" \
    "{'image-path': <'/nonexistent/luxenotify-test-icon.png'>}" 500)
missing_image_id=$(notify_id "$missing_image")
test -n "$missing_image_id"

# Resident hint: the notification is created and stays until explicitly
# closed by the app.
resident=$(gdbus call --session \
    --dest org.freedesktop.Notifications \
    --object-path /org/freedesktop/Notifications \
    --method org.freedesktop.Notifications.Notify \
    "Luxe test" 0 "dialog-information" "Resident notification" \
    "Resident hint keeps the notification until closed." \
    "['dismiss', 'Dismiss']" "{'resident': <true>, 'urgency': <byte 1>}" 5000)
resident_id=$(notify_id "$resident")
test -n "$resident_id"

# Body markup: whitelisted tags render, while hostile tags and href schemes
# (file/javascript) must be neutralized without breaking the daemon.
markup=$(gdbus call --session \
    --dest org.freedesktop.Notifications \
    --object-path /org/freedesktop/Notifications \
    --method org.freedesktop.Notifications.Notify \
    "Luxe test" 0 "dialog-information" "Markup body" \
    '<b>Bold</b> and <a href="https://example.com">link</a>' "[]" "{}" 500)
markup_id=$(notify_id "$markup")
test -n "$markup_id"

hostile=$(gdbus call --session \
    --dest org.freedesktop.Notifications \
    --object-path /org/freedesktop/Notifications \
    --method org.freedesktop.Notifications.Notify \
    "Luxe test" 0 "dialog-information" "Hostile markup" \
    '<a href="file:///etc/passwd">x</a><script>alert(1)</script><a href="javascript:alert(1)">y</a>' \
    "[]" "{}" 500)
hostile_id=$(notify_id "$hostile")
test -n "$hostile_id"

close_notification "$critical_id"
close_notification "$low_id"
close_notification "$image_id"
close_notification "$missing_image_id"
close_notification "$resident_id"
close_notification "$markup_id"
close_notification "$hostile_id"

# P1 #8a: excessive actions are capped (10 pairs). 15 pairs must still yield
# a notification without breaking the daemon; extras are ignored.
many_actions="['k0', 'L0', 'k1', 'L1', 'k2', 'L2', 'k3', 'L3', 'k4', 'L4', 'k5', 'L5', 'k6', 'L6', 'k7', 'L7', 'k8', 'L8', 'k9', 'L9', 'k10', 'L10', 'k11', 'L11', 'k12', 'L12', 'k13', 'L13', 'k14', 'L14']"
many=$(gdbus call --session \
    --dest org.freedesktop.Notifications \
    --object-path /org/freedesktop/Notifications \
    --method org.freedesktop.Notifications.Notify \
    "Luxe test" 0 "dialog-information" "Many actions" \
    "Excess action pairs are capped." "$many_actions" "{}" 5000)
many_id=$(notify_id "$many")
test -n "$many_id"
close_notification "$many_id"

# P1 #8a: live-notification cap (32). Fill past the cap with persistent
# notifications; the daemon must stay alive and the oldest id must have been
# evicted, so a replacement against it allocates a fresh id.
first=""
last=""
i=0
while [ "$i" -lt 40 ]; do
    created=$(gdbus call --session \
        --dest org.freedesktop.Notifications \
        --object-path /org/freedesktop/Notifications \
        --method org.freedesktop.Notifications.Notify \
        "Luxe test" 0 "dialog-information" "Cap fill $i" \
        "Eviction queue fill." "[]" "{}" 0)
    nid=$(notify_id "$created")
    test -n "$nid"
    if [ "$i" -eq 0 ]; then
        first="$nid"
    fi
    last="$nid"
    i=$((i + 1))
done
test -n "$first"
replaced=$(gdbus call --session \
    --dest org.freedesktop.Notifications \
    --object-path /org/freedesktop/Notifications \
    --method org.freedesktop.Notifications.Notify \
    "Luxe test" "$first" "dialog-information" "Evicted replacement" \
    "Oldest was evicted, so this must not preserve the id." "[]" "{}" 5000)
replaced_id=$(notify_id "$replaced")
test -n "$replaced_id"
if [ "$replaced_id" = "$first" ]; then
    echo "expected oldest notification $first to be evicted past the live cap" >&2
    exit 1
fi
close_notification "$replaced_id"
close_notification "$last"
