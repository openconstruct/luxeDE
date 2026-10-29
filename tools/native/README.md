# Native maintenance tools

C++17 utilities support source hygiene, isolated GUI tests and DEB packaging.
They are build tools, never installed application payloads. Shell entry points
configure and build them under `build/native-tools-host`, with `flock` protecting
concurrent test starts. Override that directory with `LUXE_NATIVE_TOOLS_BUILD_DIR`.

Requirements: CMake 3.25+, a C++17 compiler, Make and util-linux (`flock`). Package
audits additionally need libarchive development headers, dpkg and sha256sum.
The GUI runner needs X11 development headers, Xephyr and dbus-run-session.
Debian/Ubuntu package names include `build-essential`, `libarchive-dev` and
`libx11-dev`; CI installs them through `ci/dependencies.txt`.

Entry points from the repository root:

```sh
ci/check-source.sh
cmake/RunNestedX.sh COMMAND ARGUMENTS
cmake/AuditDeb.sh PACKAGE.deb EXPECTED_PACKAGE
cmake/AuditDebOwnership.sh PACKAGE1.deb PACKAGE2.deb
cmake/OrganizeArtifacts.sh --root /path/to/artifacts --incoming PACKAGE.deb
cmake -DDOWNLOAD_DIRECTORY=/path/to/downloads -DOUTPUT_DIRECTORY=/path/to/cohort -P ci/CollectDownloads.cmake
```

The organizer preserves the frozen rollback by default. `--rotate-old` selects
the previous version; `--keep-old 0` drops rollback packages; `--dry-run` reports
selection without activating it. Version ordering uses dpkg, and payload audit
and copy verification precede activation. Promotion does not install packages.

Run the package regression against disposable fixtures:

```sh
cmake -S tools/native -B build/native-tools-host
cmake --build build/native-tools-host
ctest --test-dir build/native-tools-host --output-on-failure
```

The C++ icon deduplicator is invoked through `luxe-icons/cmake/Deduplicate.cmake`.
DebInstall and the session migration runtime retain their Python dependency.
DebInstall retains its Python tests alongside its unchanged application code.
Other maintained build, diagnostic and test harnesses are native or shell.

Completed GTK/Cairo and resident-batch migration scripts were retired on
24 October 2026. Historical audit results remain in the documentation and
artifacts; those one-time source/version mutation commands are no longer tools.

## Local validation, 24 October 2026

- Native package regression and source-hygiene rejection fixtures pass.
- All seven shared GTK tests pass, including concurrent private displays;
  both freshly built Luxe Settings tests pass.
- Full icon generation passes coverage and GTK lookup checks. Its icon tree
  matches the current packaged tree, including symlink targets.
- All 24 source directories pass hygiene; 25 existing DEBs pass cross-package
  ownership checking; the 24-package collector passes in a disposable root.
- Source checks, ownership audit and artifact selection pass with Python commands
  blocked in PATH. No current or frozen rollback packages were changed.
- PCManager forwarding and session profile-boundary shell fixtures pass.
  State-directory checks pass positive/negative install fixtures. Validation
  against existing LuxuryStore builds is blocked by stale APT RPATHs; the
  original Python wrapper fails there too. The existing PCManager drag/drop
  executable fails its line-251 assertion under both old and new wrappers.
  These are not reported as passing tests or repaired application defects.

RPM generation was not run: this host has no rpmbuild. Full distro CI and all
application tests have not been rerun for this tooling change.

## Native diagnostics

All former Python diagnostics in `cmake/` and `tools/diagnostics/` now use the
C++ `diagnostics` executable. The shell entry points retain their positional
arguments and options; invoke `cmake/MeasureThemeMemory.sh` instead of
`python3 cmake/MeasureThemeMemory.py`, for example. Historical reports may still
record the old filenames. Current entry points are:

- `InspectXResources.sh`: read-only XRes client/resource ownership.
- `MeasureIconPayload.sh`: icon content, aliases, disk allocation and equivalence.
- `ProfileDMResources.sh`: private greeter resource-lifetime fixture.
- `ProfileViewerMemory.sh`: image loading, rotation and retention comparison.
- `ProfileWallpaperResources.sh`: raster-size and repeated-apply measurements.
- `ProfileWallpaperCollections.sh`: all 18 original collection images.
- `CaptureViewerPalettes.sh`: six real-artwork palette captures.
- `MeasureThemeMemory.sh`: private terminal theme/icon/renderer samples.
- `MeasureResidentMemory.sh`: sandboxed old/new resident-process samples.
- `ProfileActiveMemory.sh`: four-application workload and process metrics.
- `TraceIdleRegression.sh`: private renderer/theme/resource attribution.
- `tools/diagnostics/compare-settings.sh`: private settings-window comparison.

Except for the read-only icon report, run GUI diagnostics through
`cmake/RunNestedX.sh`. Outputs remain nested-display diagnostics, never hardware
acceptance or a whole-desktop memory guarantee. Old/new executable and historic
asset inputs must still exist; the tools do not silently substitute packages.
JSON report schemas and sampling stages are preserved. Palette and workload
screenshots now use ImageMagick `import`, and terminal scrollback generation
uses `/bin/sh`; neither launches Python. Timeout/error cleanup terminates child
process groups and removes temporary profiles. Interrupted diagnostics also
clean up their children.

Additional build dependencies: `nlohmann-json3-dev`, `libxres-dev`,
`libgdk-pixbuf-2.0-dev`, and X11/Xext development headers. Runtime commands include
`xdotool`, ImageMagick, `bubblewrap`, and the selected application binaries.
These dependencies are listed in the CI build container manifest.

Validation: all twelve diagnostic modes were exercised locally, including the
real greeter fixture, three raster sizes, viewer rotations with/without alpha,
18 collection images, six screenshots, theme sampling, sandboxed clipboard,
settings comparisons, four-app workload and panel tracing. Workload comparison
validation deliberately used identical executables on both sides; its numbers
are not a before/after optimization claim. Native regression tests cover live
proc metrics, literal environment passing, timeouts/reaping, interactive pipes,
icon reports, changed-artwork rejection and external-link rejection.

## Native application fixtures

`behavior-fixtures` now covers notification headless registration and renderer
lifetimes, authentication child boundaries, wallpaper collection installation,
XDG autostart filtering, terminal forwarding, session settings, Frame's wallpaper
daemon (including RandR settling), LuxUp's service, and panel taskbar, restart
and Run-dialog lifetimes. The package CTests use shell entry points with the
original arguments. Application sources and DebInstall's Python tests are not
rewritten. GTK3 compatibility clients are compiled C++ using `libgtk-3-dev`.

`icon-fixtures-gtk4` checks coverage, aliases, original artwork, safe links,
checksum/output guards and GTK lookup. Set `LUXE_ICON_TEST_GTK3=1` on
`luxe-icons/tests/check-lookup.sh` to use the GTK3 variant. The shell package
ownership fixture unpacks only in temporary roots and refuses preinst scripts.

`lucid/tools/import-themes.sh PINNED_CHECKOUT` runs the C++ theme importer.
All 20 generated XML schemes and the license matched the original importer in a
reconstructed palette fixture; provenance differs only by the entry-point name.
The importer validates all input palettes before writing any output files.

The panel initialization and workload profilers use `luxepanel/tests/profile-initialization.sh`
and `profile-panel.sh`. Their JSON schemas, renderer/theme matrices, catalogue
activation checks and failure artifacts are preserved. The complete workload
matrix can exceed 90 seconds: use `LUXE_TEST_TIMEOUT_SECONDS=300` with the nested
runner. The default remains 90 seconds; overrides must be 1–3600 seconds.

Package builds prepare native test helpers via `cmake/NativeTestTools.cmake`.
Direct shell entry points also build on demand through `tools/build-native.sh`.
GTK3 and libadwaita development headers are test dependencies; they are not
installed application payloads. Shared targets are built under a lock.

Additional validation: all five tests from a fresh LuxeNotify build pass; a
clean native-tools build and its three CTests pass. A fresh LuxeSession build
succeeds with Python commands blocked in PATH. Both archive-installer suites
pass with GNU tar replacing inline Python archive generation. Theme probes pass
for all six palettes under GTK3, GTK4 and libadwaita, and Openbox pointer/geometry
checks pass. The existing real-terminal header baseline fails in both original
and native harnesses (32 pixels before and after). Its full pointer/input checks
pass with a disposable deliberately tall baseline fixture; the production test
keeps its assertion and is not reported as passing.

Final validation: the full panel workload produced 28 records, including four
successful real catalogue activations, with a private null audio sink. The
initialization matrix produced 72 records. Fresh LuxeSession CTest results are
15/16 passing; the only failure is the unchanged real-header baseline described
above. The full distro matrix and RPM generation remain untested locally.
