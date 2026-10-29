# Optional luxe wallpaper collections

Horizon, River and Veil were supplied by Jerry Howell on 5 October 2026 from
`/home/jerr/Downloads/wp`. All 18 supplied files are preserved byte-for-byte:
lossless 3840 × 2160 RGB PNGs, with six palette variants per collection. No
recoloring, recompression, resolution change or runtime download is performed.
Supplied luxe artwork is distributed with LuxeSession under GPL-2.0-or-later;
copyright © 2026 Jerry Howell. No additional upstream artwork is imported here.

Installed in `/usr/share/backgrounds/luxe/horizon`, `/river` and `/veil`.
Select an individual image through Frame, or select a collection directory
for its existing daily/next-login rotation. Each collection contains all six
colors: selecting a collection directory rotates its contents without theme
filtering. Frame also rotates matching variants automatically when a bundled
orbit wallpaper is selected.

Wallpaper collection filename suffixes now use the exact palette IDs from
`cmake/LuxePaletteCatalog.cmake` (for example, `mocha` and `tokyo-night`).
Frame reads `Appearance.theme` and uses that ID directly; no separate
theme-to-filename translation is maintained. The source PNG bytes are
unchanged; only filenames and their checksum-manifest paths changed.

The six original orbit wallpapers and theme-follow defaults remain unchanged.
Only a selected wallpaper is decoded; adding these on-disk choices does not
start another daemon or preload their images. Frame retains its screen-sized
PNG cache/stretch behavior and never modifies these originals.

`WALLPAPER-COLLECTIONS.sha256` in this documentation directory lists hashes with
paths relative to `/usr/share/backgrounds/luxe`. Source copies use the same
layout and are checked by the staged-install regression test.
