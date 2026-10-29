#!/bin/sh
# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
set -eu
root=$(mktemp -d)
trap 'rm -rf -- "$root"' EXIT HUP INT TERM
program=$(realpath "${1:?program required}")
cat > "$root/frame" <<'SCRIPT'
#!/bin/sh
printf '%s\n' "$@" > "$FRAME_TEST_LOG"
exit "${FRAME_TEST_EXIT:-0}"
SCRIPT
chmod 700 "$root/frame"
export PATH="$root:$PATH" FRAME_TEST_LOG="$root/args" DISPLAY=:65530
check() {
  expected=$1
  shift
  rm -f "$FRAME_TEST_LOG"
  timeout 4 "$program" "$@"
  tries=0
  while [ ! -f "$FRAME_TEST_LOG" ] && [ "$tries" -lt 100 ]; do sleep .01; tries=$((tries+1)); done
  printf '%s\n' "$expected" > "$root/expected"
  cmp "$root/expected" "$FRAME_TEST_LOG"
}
check --wallpaper-daemon --desktop
check --wallpaper-settings --desktop-pref
check --wallpaper-stop --desktop-off
check '--set-wallpaper
/tmp/picture space.png' --set-wallpaper '/tmp/picture space.png'
rm -f "$FRAME_TEST_LOG"
if FRAME_TEST_EXIT=7 timeout 4 "$program" --set-wallpaper /tmp/failure.png; then
  echo 'Frame failure was not returned to the caller' >&2
  exit 1
fi
grep -q '^--set-wallpaper$' "$FRAME_TEST_LOG"
echo 'PASS: wallpaper compatibility argv forwards to Frame without initializing a GTK display'
