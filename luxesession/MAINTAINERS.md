# LuxeSession source and configuration ownership

The CMake build compiles the checked-in C files directly. Those C files are
the release source of truth. The neighboring Vala files are retained as
historical/reference material; this build does not regenerate C from them.
When changing a built C file, update the corresponding Vala reference in the
same change only when the mapping is clear. Never regenerate the C tree from
the Vala files without reviewing the generated diff and the build graph.

The shipped LXDE session defaults have one editable source:
`data/LXDE/desktop.conf`, installed at
`${CMAKE_INSTALL_SYSCONFDIR}/xdg/luxesession/LXDE/desktop.conf`. The
`/etc/xdg/autostart` entry is a separate autostart registration, not a second
copy of session defaults.
