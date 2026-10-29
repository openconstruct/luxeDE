#!/bin/sh
# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
set -eu
if ! systemctl --user show-environment >/dev/null 2>&1; then
  echo 'SKIP: no running user systemd manager'
  exit 77
fi
policy=${1:?service file required}
value() {
  awk -v section="$1" -v key="$2" '
    /^\[/ {active=($0=="[" section "]")}
    active && index($0,"=") {split($0,a,"="); if(tolower(a[1])==tolower(key)) {sub(/^[^=]*=/,""); print; exit}}
  ' "$policy"
}
interval=$(value Unit StartLimitIntervalSec)
burst=$(value Unit StartLimitBurst)
delay=$(value Service RestartSec)
restart=$(value Service Restart)
case "$interval:$burst:$delay" in *[!0-9:]*|::*|*::|:*|*:) echo 'Invalid restart timing' >&2; exit 1;; esac
test "$burst" -ge 2 && test "$burst" -le 3
test "$interval" -gt "$(((delay+2)*burst))"
unit="luxe-dm-restart-check-$(tr -d '-' < /proc/sys/kernel/random/uuid).service"
cleanup() {
  systemctl --user stop "$unit" >/dev/null 2>&1 || :
  systemctl --user reset-failed "$unit" >/dev/null 2>&1 || :
}
trap cleanup EXIT HUP INT TERM
systemd-run --user "--unit=$unit" "--property=Restart=$restart" "--property=RestartSec=$delay" "--property=StartLimitIntervalSec=$interval" "--property=StartLimitBurst=$burst" /usr/bin/false
stopped() {
  printf '%s\n' "$1" | grep -Fxq 'ActiveState=failed' && printf '%s\n' "$1" | grep -Fxq "NRestarts=$burst"
}
tries=$(((delay*burst+10)*10))
while [ "$tries" -gt 0 ]; do
  state=$(systemctl --user show "$unit" --property=Result,ActiveState,NRestarts)
  if stopped "$state"; then
    sleep "$((delay*2+1))"
    later=$(systemctl --user show "$unit" --property=Result,ActiveState,NRestarts)
    stopped "$later" || { printf '%s\n' "$later" >&2; exit 1; }
    printf 'PASS: restart limit stopped the failing test service\n%s\n' "$state"
    exit 0
  fi
  sleep .1
  tries=$((tries-1))
done
printf 'Restart loop was not bounded: %s\n' "$state" >&2
exit 1
