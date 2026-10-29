#!/bin/sh
# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
set -eu
runtime="/run/user/$(id -u)"
# A logind runtime directory is not guaranteed in a container or CI runner.
[ -d "$runtime" ] && [ -O "$runtime" ] && [ "$(stat -c %a "$runtime")" = 700 ] || exit 77
root=$(mktemp -d)
session=''
cleanup() {
  [ -z "$session" ] || kill "$session" 2>/dev/null || :
  rm -rf -- "$root"
}
trap cleanup EXIT HUP INT TERM
mkdir -p "$root/home" "$root/config/luxesession/Test" "$root/cache" "$root/system" "$root/bin"
cat > "$root/bin/test-wm" <<'WM'
#!/bin/sh
printf '%s' "$XDG_RUNTIME_DIR" > "$RUNTIME_PROBE_FILE"
exec sleep 10
WM
chmod 700 "$root/bin/test-wm"
cat > "$root/config/luxesession/Test/desktop.conf" <<'CONF'
[Session]
window_manager=test-wm
disable_autostart=all
CONF
export HOME="$root/home" XDG_CONFIG_HOME="$root/config" XDG_CACHE_HOME="$root/cache"
export XDG_CONFIG_DIRS="$root/system" RUNTIME_PROBE_FILE="$root/runtime"
export PATH="$root/bin:$PATH"
env -u XDG_RUNTIME_DIR "$1" -s Test -e Test -n -a > "$root/session.log" 2>&1 &
session=$!
i=0
while [ ! -s "$root/runtime" ] && [ "$i" -lt 50 ]; do
  kill -0 "$session" 2>/dev/null || break
  sleep .1
  i=$((i + 1))
done
if [ ! -s "$root/runtime" ] || [ "$(cat "$root/runtime")" != "$runtime" ]; then
  cat "$root/session.log"
  cat "$root/cache/luxesession/Test/run.log" 2>/dev/null || :
  exit 1
fi
echo 'PASS: session child inherits the recovered runtime directory'
