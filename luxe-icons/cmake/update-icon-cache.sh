#!/bin/sh
set -e
icon_dir="@ICON_CACHE_DIR_SHELL@"
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
  for theme in Luxe Luxe-Light Luxe-Dark hicolor; do
    if [ -f "$icon_dir/$theme/index.theme" ]; then
      gtk-update-icon-cache -q -t -f "$icon_dir/$theme" || true
    fi
  done
fi
exit 0
