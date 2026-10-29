#!/bin/sh
set -eu

root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT
fake_snap="$root/snap"
state="$root/state"
log="$root/log"
hold="$root/hold"

cat > "$fake_snap" <<'EOF'
#!/bin/sh
case "$*" in
    "refresh --time")
        [ "${FAIL_TIME:-0}" != 1 ] || exit 1
        if [ -e "$FAKE_HOLD" ]; then printf 'hold: %s\n' "$(cat "$FAKE_HOLD")"; else echo "timer: 00:00~24:00/4"; fi
        ;;
    "refresh --hold=forever")
        echo hold >> "$FAKE_LOG"
        printf forever > "$FAKE_HOLD"
        [ "${FAIL_AFTER_HOLD:-0}" != 1 ] || exit 1
        ;;
    "refresh --unhold")
        [ "${FAIL_UNHOLD:-0}" != 1 ] || exit 1
        echo unhold >> "$FAKE_LOG"
        rm -f "$FAKE_HOLD"
        ;;
    *) exit 2 ;;
esac
EOF
chmod +x "$fake_snap"

export LUXUP_SNAP_COMMAND="$fake_snap" LUXUP_STATE_DIR="$state"
export FAKE_HOLD="$hold" FAKE_LOG="$log"
policy="$(dirname "$0")/../data/luxup-snap-policy"

sh "$policy" apply
[ -e "$state/snap-auto-refresh-held" ]
[ "$(cat "$log")" = hold ]

sh "$policy" apply
[ "$(wc -l < "$log")" -eq 1 ]

sh "$policy" remove
[ "$(tail -n 1 "$log")" = unhold ]
[ ! -e "$state/snap-auto-refresh-held" ]

printf forever > "$hold"
: > "$log"
sh "$policy" apply
[ ! -e "$state/snap-auto-refresh-held" ]
[ ! -s "$log" ]

# Never modify policy when querying it fails.
rm -f "$hold"
if FAIL_TIME=1 sh "$policy" apply; then exit 1; fi
[ ! -e "$hold" ]
[ ! -e "$state/snap-auto-refresh-held" ]

# Keep our ownership marker if reversal fails so removal can be retried.
sh "$policy" apply
if FAIL_UNHOLD=1 sh "$policy" remove; then exit 1; fi
[ -e "$state/snap-auto-refresh-held" ]
sh "$policy" remove
[ ! -e "$state/snap-auto-refresh-held" ]

# A prior finite hold is never replaced by a permanent one.
printf '2026-12-01T12:00:00Z' > "$hold"
: > "$log"
sh "$policy" apply
[ "$(cat "$hold")" = '2026-12-01T12:00:00Z' ]
[ ! -e "$state/snap-auto-refresh-held" ]
[ ! -s "$log" ]
rm "$hold"

# A failed record write cannot alter Snap policy.
mkdir "$state/snap-auto-refresh-held"
if sh "$policy" apply; then exit 1; fi
[ ! -e "$hold" ]
rmdir "$state/snap-auto-refresh-held"

# Even a command that changes policy and then fails leaves recovery evidence.
if FAIL_AFTER_HOLD=1 sh "$policy" apply; then exit 1; fi
[ "$(cat "$hold")" = forever ]
[ -f "$state/snap-auto-refresh-held" ]
sh "$policy" remove
[ ! -e "$hold" ]
[ ! -e "$state/snap-auto-refresh-held" ]

# A subsequent administrator's finite hold survives removal.
sh "$policy" apply
printf '2027-01-02T12:00:00Z' > "$hold"
sh "$policy" remove
[ "$(cat "$hold")" = '2027-01-02T12:00:00Z' ]
[ ! -e "$state/snap-auto-refresh-held" ]
rm "$hold"

# An identical forever hold cannot be attributed to an actor by Snap. Once
# LuxUp has recorded ownership, its package lifecycle remains authoritative.
sh "$policy" apply
printf forever > "$hold"
sh "$policy" remove
[ ! -e "$hold" ]
[ ! -e "$state/snap-auto-refresh-held" ]

# Simultaneous apply calls share the lock and issue one hold.
: > "$log"
sh "$policy" apply & p1=$!
sh "$policy" apply & p2=$!
wait "$p1"
wait "$p2"
[ "$(grep -c '^hold$' "$log")" -eq 1 ]
sh "$policy" remove
