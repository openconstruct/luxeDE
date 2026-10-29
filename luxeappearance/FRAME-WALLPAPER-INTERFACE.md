# Frame wallpaper interface used by Luxe Appearance

Frame owns wallpaper selection and persists its source in
`$XDG_CONFIG_HOME/luxe/wallpaper.ini` (`[Wallpaper] source`). Luxe Appearance
applies a bundled preset by running `frame --set-wallpaper PATH`, waits for the
command to finish, then reads that key and records the source in its own
`[Appearance] last-wallpaper` state only when Frame reports the requested
source. On later opens, Appearance compares the Frame-owned source with its
last successful application; a difference means the user changed wallpaper
elsewhere and turns preset following off.

Appearance never reads or writes PCManager wallpaper state. Frame performs any
one-time import of legacy PCManager settings. The config path and field belong
to Frame; the file is a read-only status interface for Appearance, while
changes must go through Frame's CLI so decode, persistence, and X11 publication
share the same error handling.
