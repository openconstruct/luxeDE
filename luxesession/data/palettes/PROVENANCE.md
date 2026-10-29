# luxe global theme sources

Copyright (C) 2026 Jerry Howell. Original palette mapping, CSS integration,
Openbox variants and SVG landscapes are GPL-2.0-or-later.

Six selectable themes: luxe Light, luxe Dark, Nord, Catppuccin Mocha, Dracula
and Tokyo Night. These are luxe adaptations, not officially endorsed GTK ports.
Light retains the softened grey direction. Theme assets do not include editor
schemes; Lucid/LuxeTerm optionally follow the desktop palette through their
own preferences, retaining custom colors when following is switched off.

Complete Adwaita widget geometry, states and assets are imported from the GTK
runtime's own resources: GTK3 `Adwaita/gtk.css` or `gtk-dark.css`, GTK4
`Default/gtk.css` or `gtk-dark.css`. GTK is LGPL-2.1-or-later. No installed
package files are copied into a build or redistributed as vendored GTK code.
This intentionally replaces the proposal's pinned external base with the
version-matched base already supplied by GTK. The resource layout is verified
against GTK 4.14.5 source and the local GTK 3.24/4.18 runtimes:

- https://github.com/GNOME/gtk/blob/4.14.5/gtk/gen-gtk-gresources-xml.py
- https://docs.gtk.org/gtk4/class.CssProvider.html
- https://docs.gtk.org/gtk3/class.CssProvider.html

Palette anchors are from the maintainers' published palettes. Semantic roles,
muted text/borders, contrast-safe action text and landscapes are luxe's mappings:

- Nord: https://www.nordtheme.com/docs/colors-and-palettes/
- Catppuccin Mocha: https://github.com/catppuccin/catppuccin
- Dracula: https://draculatheme.com/contribute
- Tokyo Night: https://github.com/folke/tokyonight.nvim

The libadwaita bridge uses documented color overrides (legacy named colors,
and custom properties on GTK >= 4.16), plus scoped compact headerbar-node
styling shared with the full GTK themes. No libadwaita stylesheet is replaced.
Header minimums and padding shrink; fonts remain unchanged, and larger titles,
subtitles or rich controls may expand naturally. Body/popover buttons retain
their normal sizing. This geometry adaptation is not an official full-theme
API. Native GTK3 3.24, GTK4 4.18 and libadwaita 1.7 behavior is tested locally;
other supported distro runtimes still need user QA. Third-party apps may require
restart. Foreign tray pixels, Qt/Electron and sandbox theme delivery are not
claimed as complete coverage. Flatpak extensions/Snap content themes require
separate packaging; no sandbox filesystem overrides are added.

- https://github.com/GNOME/libadwaita/blob/1.5.0/doc/named-colors.md
- https://gnome.pages.gitlab.gnome.org/libadwaita/doc/1-latest/css-variables.html

Openbox variants derive their flat geometry and glyphs from the retired
Syscrash-Luxe pair; only the six palette variants are now installed. The
29 September compact update reduces shared spacing to 5 px horizontal/0 px
vertical with original 6x6 button masks shared across all six variants.
Title fonts remain user-controlled; Openbox derives actual heights and hit
targets from font metrics. That Openbox change alone does not affect GTK bars;
the separate shared GTK follow-up compacts client-side headers too. All six
wallpapers have original SVG sources generated from the checked-in landscape
template and palette-specific silhouettes. Since 3 October the build renders
these through rsvg-convert into lossless 3840x2160 PNGs; only PNGs are installed.
The build requires rsvg-convert (Debian: librsvg2-bin), not a new runtime daemon
or wallpaper engine. Default/theme-follow presentation is stretch to cover
non-16:9 displays; personal wallpaper files and optional follow choices remain.
No external wallpapers or runtime downloads.
This describes the six generated default wallpapers. Separately, the optional
Horizon/River/Veil PNG collections supplied by Jerry on 5 October are staged in
`data/wallpapers/` for the next build. Their unchanged source hashes and separate
provenance ship in WALLPAPER-COLLECTIONS.md/.sha256; they do not replace defaults.
29 September follow-up: the two-ring, eight-ray orbit motif is a clean vector
redraw of the reference supplied by Jerry Howell (`lumen.webp`); the supplied
raster file remains unchanged. Light/Dark use quieter translucent folds; all
six receive the shared motif and small lowercase luxe wordmark. No font asset
is redistributed; the wordmark uses the renderer's sans-serif fallback.
