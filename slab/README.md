# slab

An original Plank-like GTK4/Cairo dock, not a port of Plank source.
Copyright (C) 2026 Jerry Howell. GPL-2.0-or-later.

Build and run manually (set `LUXE_ROOT` to your checkout):

```sh
LUXE_ROOT="$PWD"   # from the repository root
cmake -S $LUXE_ROOT/slab -B $LUXE_ROOT/build/slab
cmake --build $LUXE_ROOT/build/slab
$LUXE_ROOT/build/slab/slab
```

Building alone installs nothing. A DEB can be generated
with `cpack --config $LUXE_ROOT/build/slab/CPackConfig.cmake -G DEB`.
It installs `slab` and `slab-settings` with separate menu launchers, not autostart. Slab is included in the release matrix and supports X11.
After upgrading, use slab Settings Stop then Start to run the new executable;
package installation deliberately does not restart your running dock.
`slab --reveal` only reveals an existing dock and does nothing when it is
stopped. LuxePanel luxe44 sends the same non-activating D-Bus action when the
menu shortcut runs, retaining menu keyboard focus. No second Super binding
is needed and no slab dependency or automatic startup is imposed on Panel.
Use slab Settings to start/stop the dock, adjust icon size and autohide, or reorder/unpin applications.
Settings also owns icon gaps, button padding, dock margins, background opacity and
optional short hover transitions (off by default, respecting GTK animation
preferences). Opacity requires an X11 compositor. There is no zoom/bounce or
continuous animation. An optional installed GTK theme name overrides desktop
following; clear it to follow again. Icon themes continue following the desktop.
About is only in Settings, never in the dock.
Tab/Enter navigate and activate buttons; right-click or Shift+F10 opens an
application's menu, including its window list, launch and pin/unpin actions.

Optional `~/.config/slab/pins.txt` contains one installed desktop-file ID per
line (maximum 32 entries, 64 KiB). An empty file means no pins. If absent,
defaults are LuxeTerm, PCManager and Settings. Unavailable launchers are skipped
without deleting their saved entries. Corrupt or symlinked files are refused,
not silently replaced. Pin changes are saved atomically; a failed save leaves
the previous in-memory order intact.

The dock follows the desktop GTK and icon themes live, with 20-pixel launcher icons, 2-pixel gaps and
no zoom, bounce or slide animation. Running windows merge with their pinned
launcher using GTK application IDs, StartupWMClass and desktop/executable names.
Unmatched windows group by WM_CLASS with a generic icon. A ring of themed dots indicates
running windows (one dot per window, larger contrasting dots for the active app);
clicking focuses or cycles them, or launches an idle pin.
Apps with unusual window identifiers may need matching desktop metadata.

Drag an installed `.desktop` file into the dock to pin it. Drag a recognized
running-app icon into the pin area, or use Keep in dock. Drag pins to reorder.
Drops save the order immediately. Arbitrary downloaded desktop files and
commands are rejected; install a launcher in an XDG applications directory
first. Single-file drops only. Drop support uses GTK4's native drag/drop APIs.

Autohide waits 700 ms after leaving. A 12-logical-pixel activation strip at
the bottom of the current work area, at least 96px wide, reveals it without stealing focus. When a
bottom panel reserves space, the strip sits just above that panel. Keyboard
focus, open menus and active drags keep the dock visible. The bottom gap also
keeps it visible while crossing from the edge; a delayed pointer check is used
only while shown, never while hidden. Starting slab again
reveals the existing instance. Autohide and icon size persist through GSettings.

X11 property events update the model without polling. Unchanged groups retain
their widgets and keyboard focus. Application metadata is cached and refreshed
on installation changes. There is no repeating animation or autohide timer.

Limitations: X11 only; no per-monitor placement policy or intelligent dodge
mode yet. Position uses the current desktop's advertised work area and reserves
no extra screen space. The revealed dock may overlap ordinary windows. Wayland
requires a separate integration backend. No hardware memory target is claimed.

Tests use the repository's private nested X display, never the live desktop:

```sh
ctest --test-dir $LUXE_ROOT/build/slab --output-on-failure
```

Tests cover grouping/cycling, pin persistence and validation, installed-file
drops, keyboard menus, unpin, window disappearance and autohide state. With
Openbox and xdotool installed, a separate nested test exercises real mouse-drag
reordering, focus requests, delayed hide and edge reveal. GUI tests require a
host X display plus Xephyr and dbus-run-session, but never use the live desktop.

Validation (12 October 2026): all four tests passed, including settings bindings,
live theme changes, the remote stop action, and real pin-reorder
gestures under Openbox. The Openbox test also passed at 2× scale on a 1280×720
nested display. Address/undefined-behavior sanitizer runs passed with leak
detection disabled (not a leak audit). The isolated hidden-idle sample consumed
less than 1 ms of process CPU over one second; this is not a target-hardware
benchmark or a whole-desktop memory result.
