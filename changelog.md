## 29 October 2026 — Shotty CI cache cleanup

Keep the check that Shotty removes its own handoff cache, then clean other library-created entries from the private test cache without following symbolic links. The old teardown reproduced the reported failure with an extra cache directory; the fixed suite passed with and without that fixture, including a nested-cache and outside-symlink regression. The Debian package builds; GitHub jobs still need rerunning.

## 29 October 2026 — PCManager clean-checkout packaging (luxe44)

Generate both desktop launchers from tracked templates rather than installing an ignored file. The clean-source Ninja build, existing tests, staged-install check, translation fixture and DEB packaging passed.

## 29 October 2026 — LuxuryStore CI state directory

LuxuryStore CI creates the state directory normally provided by its package before unprivileged tests. Its absence reproduced all four reported failures; a fresh build passed all nine CTests once the directory was supplied. The tests now print APT load errors clearly.

## 29 October 2026 — LuxeSettings CI locale fix

LuxeSettings CI jobs install locale data and generate de_DE.UTF-8 to retain the real German translation test. Both CTests passed with a freshly generated private locale after reproducing the missing-locale failure.

## 29 October 2026 — LuxeSession CI fixture fixes

The MIME D-Bus test uses isolated HOME and XDG directories without nested Bubblewrap; the Openbox test waits for the client and window manager to start. All 23 tests in the reported CI configuration passed locally with Bubblewrap blocked. A separate optional installed-LuxeTerm header test timed out locally and was absent from that CI configuration.

## 29 October 2026 — LuxeRandR Ubuntu query test fix

Restore executable permissions after each temporary xrandr fixture rewrite; Ubuntu GLib 2.80 otherwise drops them. All six CTests passed on Debian, and the failure and fix were checked with simulated Ubuntu mode loss.

## 29 October 2026 — GitHub artifact and Panel CI fixes

Escape epoch colons in CI DEB filenames before checksumming and upload, retaining the package version. The 25-package cohort collector passed. Panel builds unprivileged, installs into the disposable job container, and runs tests and packaging unprivileged without nested Bubblewrap. All 27 Panel tests passed across staged Debian runs; GitHub jobs still need rerunning.

## 29 October 2026 — Luxe Icons Ninja build fix (revision 13)

Move the build stamp outside the generated theme directory so Ninja does not pre-create an unowned output. Incremental builds validate ownership and regenerate missing output. A fresh Ninja build, incremental rebuild, five tests and DEB packaging passed on Debian.

## 29 October 2026 — Lucid spell-check teardown fix (luxe23)

Disable spell-check before releasing a document buffer, preventing a queued callback in Ubuntu 24.04 libspelling 0.2 from accessing a destroyed buffer. Both CTests passed with libspelling 0.2 and 0.4.8 on a Debian host.

## 29 October 2026 — DebInstall Debian 13 cache fix (0.1.0-17)

Open the normal system APT cache when inspecting packages so Debian 13 deb822-only configurations do not trigger a privileged attempt to create sources.list. All ten CTests passed; backend and UI also passed with a read-only APT directory and no sources.list.

## 29 October 2026 — Shotty clipboard fix (luxe22)

Shotty offers one PNG clipboard format so the clipboard manager can retain it for pasting after Shotty closes. The release build and behavior tests passed; a separate full-size cross-process clipboard-manager test reproduced the old failure and passed with the fix.

## 29 October 2026 — clean source copy and Slab promotion

Slab is included in the standard 25-package GitHub Actions build and cohort on Ubuntu 24.04 and Debian 13. Its CPack output uses the shared CI package directory. The clean source copy excludes generated build products, caches, packages and crash dumps while retaining build and test sources.

# Luxe Changelog

## 29 October 2026 — complete Slab audit and promote testing6

Slab now refreshes pin settings on external file replacement while preserving
the selected ID, and dock/Settings pin writers remain serialized with stale
write detection. X event handling caches atoms, processes no more than 128
events per main-loop turn, updates active/title presentation locally, and
avoids rebuilding all window groups for title storms. Settings derive widget
ranges from the schema and update only the affected presentation property.
Work-area geometry rejects hostile origins/endpoints before positioning.

Fresh Release build and all five CTests passed, including nested Openbox,
metadata update/removal, multi-process lock/conflict, live pin monitor,
selection retention, 1,000-event title storm with a GTK heartbeat, and
malformed work-area values. CPack generated `slab_0.2.9~testing6_amd64.deb`;
metadata and payload audits passed with no set-ID entries. Testing6 is current
in `artifacts/debs`; exact previous testing5 is in `artifacts/old`. Both
25-package checksum manifests and payload audits pass. Current SHA256:
`9a0a075a5806b63d78ed8f64daa900c4f3116e597d174dfb436beeb1d73d3e1a`;
previous SHA256:
`d3c156919f816473de24abd66e320b581132f11fd1c06d4bf59bdc1c09b8a9a5`.
The experimental X11-only scope remains explicit. Audit progress is 25/25
packages with all 124 findings closed. No live dock session or package
installation ran.

## 29 October 2026 — complete Shotty audit and promote luxe21

Screenshot saves now use private same-directory staging, no-replace creation,
atomic exchange for existing files, inode verification, and directory sync.
Directory-sync failure restores the approved original when the destination is
still owned by the save; uncertain races retain a named recovery copy. Viewer
handoffs use a private one-day cache, remove only expired generated files,
and enforce 32-file/512-MiB limits. Closing Shotty no longer invalidates a
delayed viewer path. Capture geometry is checked before allocation; errors
are visible and oversized full-screen capture returns the menu in region mode.

Fresh Release build and the Xephyr behavior CTest passed. Coverage injects
write, durability, destination-race, cache-expiry/quota, cancellation, invalid
geometry and capture-error cases. CPack generated `shotty_0.1.0-1+luxe21_amd64.deb`;
metadata and payload audit passed with no set-ID files. Luxe21 is current in
`artifacts/debs`; exact previous luxe20 is in `artifacts/old`. All 25 current
and previous DEB pairs passed checksum and payload audits. Current SHA256:
`38d8037935603054f0ba498b435e3802ae78ffea42adc05bdadbe1862b29608a`;
previous SHA256:
`fe2473cb824a30aac8dceda20f1c1c89bf4d01712737e0e8426eaf7839d8a8b5`.
RPM packaging remains unavailable because `rpmbuild` is not installed. Audit
progress is 24/25 packages complete, with 120 of 124 findings closed and four
Slab findings remaining. No real screenshot or package installation ran.

## 29 October 2026 — complete PCManager audit and promote luxe43

PCManager IPC now validates peer credentials, detects an old unversioned
instance without sending it an unsupported request, and retains bounded v2
framing/ACK behavior. File copies bound local staging cleanup, report retained
remote staging, and surface metadata failures. Monitor storms are coalesced,
hidden tabs defer refresh, and disk-space checks no longer restart for every
directory event. Successful external cuts clear only the unchanged clipboard
owner; partial operations retain their input list and report possible partial
completion. Preference writes lock and reload before merging changed keys.
Frame wallpaper commands preserve supported mode, return finite child failures,
and no longer suppress a requested browser window.

Fresh Release build and all 10 CTests passed, including nested-X drag/drop,
clipboard-owner, stalled IPC, lock-symlink, and Frame failure cases. CPack
generated `pcmanager_1.4.0+gtk4-4+luxe43+desktop-cairo3_amd64.deb`; DEB
metadata and payload audits passed with no set-ID entries. Luxe43 is current
in `artifacts/debs`; exact previous luxe42 is in `artifacts/old`. Both
25-package checksum manifests and payload audits pass. Current SHA256:
`60eb117ab9b8b78d70378056c8cefa6817d62535f4b0743eef73d868811300b7`;
previous SHA256:
`accf5e3c5c979702dc8a0bba0922c38fccd553fd15c5e1c33fa7946e20b8668a`.
RPM packaging remains unavailable because `rpmbuild` is not installed. Audit
progress is 23/25 packages complete, with 117 of 124 findings closed and the
seven Shotty/Slab findings remaining. No package was installed and no live
file operation or desktop setting was changed.

## 29 October 2026 — complete LuxuryStore audit and promote 0.1.0-33

APT solver state and store-catalog/search state now have separate owners. APT
row and plan snapshots keep GTK presentation callbacks responsive during a slow
resolver. Flatpak/Snap commands stream output through bounded nonblocking
readers, use per-command and whole-catalog deadlines, cap catalogs at 100,000
rows, and cancel/reap superseded process groups. Closing cancels searches while
remaining blocked during a package transaction or remote mutation. Repository
updates use a root-owned lock, compare-before-replace, and conflict-safe
rollback; no-op toggles skip APT refresh.

Fresh Release builds for APT ABI 6 and 7 passed all nine CTests. CPack generated
`luxurystore_0.1.0-33_amd64.deb`; metadata and payload audits passed with no
set-ID entries. Revision 33 is current in `artifacts/debs`, and exact previous
revision 32 is in `artifacts/old`. Both 25-package checksum manifests pass and
the current manifest has 25 entries. Current SHA256:
`5cc54655dc76ebc57a40fb831630693cb2c83161fe9fe4deb3521afe841d2f19`; previous
SHA256: `e314cc6b5652cea953a0950c68a5a3ed3e2bf61105ea7eff17ae68cd4812e3a2`.

## 29 October 2026 — complete LuxePower audit and promote 0.2.0-26

Sleep-mask edits now have isolated transaction tests for both mask scopes,
every forward-operation failure, restoration, and an explicitly reported
rollback failure. TLP configuration replacement rechecks the reviewed digest
immediately before rename, preserves detected external edits, and attempts to
restore the reviewed original if the post-rename directory flush fails. The
remaining check-to-rename race against editors that ignore LuxePower's lock is
documented. The idle monitor cancels its bounded process-group probes on
shutdown and exits unsuccessfully when its startup source or schema is missing.
The idle-lock warning is tested through both set and clear changes; brightness
and battery regressions remain covered.

Fresh Release build and all six CTests passed. CPack generated
`luxepower_0.2.0-26_amd64.deb`; package metadata and payload audit passed with
no set-ID entries. LuxePower 0.2.0-26 is current in `artifacts/debs`, and exact
previous 0.2.0-25 is in `artifacts/old`. Both 25-package checksum manifests
pass, and the current manifest has 25 entries. Current SHA256:
`e772eaff899a587f0399af52fe7404c2187445e5a022ce826fa70ccabce2d8a3`; previous
SHA256: `0bbfcba0817f5c5c2874c59cf57fb937b18be0a0cac903f895afc3dfcba57f6d`.
Audit progress is 21/25 packages, with 120 findings resolved and 4 open in
the four remaining packages. No privileged helper or live power operation ran.

## 28 October 2026 — complete LuxUp audit and promote luxe33

Closed UP-01 through UP-04. Snap policy changes now have a durable transaction
record, serialized apply/remove, retryable recovery, preservation of existing
finite/forever holds, and explicit installed-package ownership semantics when
Snap cannot distinguish an identical later `forever` value. Package hooks emit
actionable watcher/service failure diagnostics. APT status expires after 24
hours without a successful LuxUp refresh; stale or unverified lists remain
unknown, and external refreshes can only cause a conservative false-stale
result. Flatpak identities retain scope and full ref, subprocess output is
bounded, and the existing whole-queue LuxuryStore review is documented as the
intended flow rather than a selected-row handoff.

Fresh Release build and all four CTests passed, including Snap failure/retry
and ownership cases, generated DEB/RPM hook syntax, capped process output, and
service restart/stale-reply transitions. CPack generated
`luxup_0.1.1-1+luxe33_amd64.deb`; metadata and payload audit passed with no
set-ID files. Luxe33 is current in `artifacts/debs`, luxe32 is in
`artifacts/old`, and both 25-package checksum manifests pass. Current SHA256:
`3ed8d0a2a0dea551198d4e0dbf1a2c0ec35f4541be812708b3a04ed19cd00630`; previous
SHA256: `9d9a765bf46d2b535db3479f0a133906b25d8923ef570937ae201f663baac7d9`.
Audit progress is 20/25 packages complete, with 116 findings resolved and 8
open/partial across LuxePower, LuxuryStore, PCManager, Shotty, and Slab.

## 28 October 2026 — surface Snap watcher lifecycle failures, release luxe32

Package hooks now report failures to enable the Snap path watcher or stop the
path/service during removal, naming the systemd unit and recovery action.
Cleanup continues and the Snap-policy helper remains serialized, so diagnostics
do not strand package removal. CTest now syntax-checks the generated DEB and
RPM hooks.

Fresh Release build and all four CTests passed. CPack generated
`luxup_0.1.1-1+luxe32_amd64.deb`; metadata and payload audits passed with no
set-ID files. Luxe32 is current in `artifacts/debs`, luxe31 is in
`artifacts/old`, and both 25-package checksum manifests pass. Current SHA256:
`9d9a765bf46d2b535db3479f0a133906b25d8923ef570937ae201f663baac7d9`; previous
SHA256: `933a1afc1911dc3a32ba67c7bd5fbeb2420edbff9bb2374be5f9605b2eb5c33d`.
UP-01 remains partial because Snap cannot expose ownership of the effective
forever hold. UP-02 and UP-03 also remain open/partial. Overall progress is
19/25 packages, with 113 findings resolved and 11 open/partial.

## 28 October 2026 — recognize Snap no-hold schedule, release luxe31

Snap's normal `refresh --time` output can omit `hold:` when no hold is
configured. LuxUp now considers a complete timer/last/next schedule evidence
for no hold; partial schedules and malformed hold values remain unknown.
Explicit empty, `forever`, and RFC 3339/ISO-8601 hold values remain supported.
See [Snap's update documentation](https://snapcraft.io/docs/how-to-guides/manage-snaps/manage-updates/)
for the normal schedule and hold-value format.

Fresh Release build and all three CTests passed. CPack generated
`luxup_0.1.1-1+luxe31_amd64.deb`; metadata and payload audits passed with no
set-ID files. Luxe31 is current in `artifacts/debs`, luxe30 is in
`artifacts/old`, and both 25-package checksum manifests pass. Current SHA256:
`933a1afc1911dc3a32ba67c7bd5fbeb2420edbff9bb2374be5f9605b2eb5c33d`; previous
SHA256: `c3f13988bee907816ce87ea28aaa3e6a6ec2626a2333da4b7ec5edebb9042792`.
UP-04 is closed. UP-01 through UP-03 remain open/partial; overall progress is
19/25 packages, with 113 findings resolved and 11 open/partial.

## 28 October 2026 — validate Snap hold values, release luxe30

Snap policy parsing accepts only an empty hold, `forever`, or a valid
ISO-8601 date/time. Timer/last/next fields and malformed hold values leave the
policy unknown instead of implying updates are unpaused. Tests cover absent,
malformed, finite and forever holds while retaining update enumeration.

Fresh Release build and all three CTests passed. CPack generated
`luxup_0.1.1-1+luxe30_amd64.deb`; metadata and payload audits passed with no
set-ID files. Luxe30 is current in `artifacts/debs`, luxe29 is in
`artifacts/old`, and both 25-package checksum manifests pass. Current SHA256:
`c3f13988bee907816ce87ea28aaa3e6a6ec2626a2333da4b7ec5edebb9042792`; previous
SHA256: `0b36eae86ef538e4ee50f71fbe9bc541c6506fb116f37fe373bcbfb66e0212b2`.
UP-04 is closed. LuxUp remains unfinished on UP-01 through UP-03: Snap cannot
distinguish an administrator's later forever hold from LuxUp's identical
hold, external APT refreshes do not update LuxUp's freshness timestamp, and
LuxuryStore has no exact identity handoff yet. Overall progress remains 19/25
packages, with 113 findings resolved and 11 open/partial.

## 28 October 2026 — LuxUp Snap policy-state correction, release luxe29

Snap scheduling output now marks automatic-refresh policy as known only when
it includes an explicit `hold:` field. Timer, last-run, and next-run fields
cannot prove whether a hold is active, so missing/malformed hold data remains
unknown while package-update detection continues. A regression fixture covers
timer/last output without a hold field.

UP-04 is closed. The UI uses asynchronous cancellable D-Bus requests; service
owner transitions invalidate stale generations and reconnect with fresh state.
The nested-X behavior fixture exercises delayed replies, name loss/reacquisition,
stale-state rejection, and a local fallback that must not overwrite the recovered
service result. The explicit `--check` CLI path is one-shot, while the resident
tray's single check authority remains the D-Bus service.

Fresh Release build and all three CTests passed. CPack generated
`luxup_0.1.1-1+luxe29_amd64.deb`; metadata and payload audits passed with no
set-ID files. Luxe29 is current in `artifacts/debs`, luxe28 is retained in
`artifacts/old`, and both 25-package checksum manifests pass. Current SHA256:
`0b36eae86ef538e4ee50f71fbe9bc541c6506fb116f37fe373bcbfb66e0212b2`; previous
SHA256: `8cd2fbab31215e85cd2b7212982818e291f425311f4b2fe94887525b3aae953c`.
LuxUp remains unfinished while UP-01 through UP-03 are open/partial. Snap cannot
distinguish an administrator's later forever hold from LuxUp's identical hold;
APT freshness does not observe successful refreshes run by other tools; and
LuxuryStore's handoff accepts no exact package identity. Audit progress remains
19/25 packages, with 113 findings resolved and 11 open/partial.

## 28 October 2026 — complete LuxeTerm audit, release luxe20

Closed TERM-01 through TERM-04. Forwarded launches preserve the caller's
environment and resolve commands against its PATH; new tabs/windows inherit
the environment and working directory of their terminal. Manual close
protection checks the interactive shell's PTY foreground process group: idle
shells close without a warning, while foreground jobs warn and remain intact
when the user cancels. Direct commands and unreadable job state fail
conservatively. Child exit remains distinct from user close. Preferences are
single-instance, theme changes apply only colors, and schema metadata owns
numeric ranges and the custom preset index. Unlimited scrollback shows its
storage implications.

Both CTests passed in a fresh Release build. Nested-X coverage runs an isolated
`/bin/sh`, verifies idle close, foreground `sleep` warning/Cancel, interrupt
and return to idle; it also covers all close routes, external environment,
PATH, cwd, `--no-remote`, preference singleton/palette behavior, schema ranges
and real child exit. CPack generated
`luxeterm_0.3.2+gtk4-2+luxe20_amd64.deb`; the payload audit found no set-ID
entries. Luxe20 is current in `artifacts/debs`, exact previous luxe19 is in
`artifacts/old`, and all 25 current/previous DEBs pass checksum verification.
Current SHA256:
`028477a3ac206ac1c79079be45396691ddbe36950668cf4f0d0b5258b4fd0fa2`; previous
SHA256: `0104fae587583fef13192c4394820987f251eafd1e395b4cc38befb82be5bc8d`.
Audit progress is now 19/25 packages, with 112 findings resolved and 12
open/partial.

## 28 October 2026 — complete LuxeTask audit, release luxe18

Closed TASK-01 through TASK-03. Process snapshots, `/proc` reads and first-time
NSS lookups now run in one background worker, with a single in-flight refresh
and main-context-only GTK updates. Username cache entries refresh every minute
and the cache is capped at 256 UIDs. Monotonic capture timestamps drive
per-process CPU deltas. Failed process enumeration preserves existing rows;
memory validity is tracked separately and invalid samples display as unknown.
PID/start-time validation and pidfd signalling remain in place. Priority
changes revalidate immediately before `setpriority`; Linux has no pidfd-based
priority API, so the remaining check/use race is documented.

The nested-X test injects delayed collection and verifies event-loop
responsiveness, one active worker under repeated refresh, and safe close during
a pending sample. Parser tests cover malformed and overflowed memory fields;
the PID test uses only itself and a child it creates. Both CTests passed in a
fresh Release build. CPack generated
`luxetask_0.1.9+gtk4-1+luxe18_amd64.deb`; metadata and payload audits passed
with no set-ID entries. Luxe18 is current in `artifacts/debs`, exact previous
luxe17 is in `artifacts/old`, and all 25 current/previous DEBs pass checksum
verification. Current SHA256:
`bd9d19e1ef3e2080f64b418705326079608bb00a357f97283121a6f1490b858d`; previous
SHA256: `14f900ab949c653fff9c3e29fb195b820b8bb1e2b6e2c7e929a3e9bac6a77306`.
No host task manager or unrelated process was touched. Audit progress is now
18/25 packages, with 108 findings resolved and 16 open/partial.

## 28 October 2026 — complete LuxeSession audit, release luxe50

Closed SES-01 through SES-08. Clipboard saves now bound retained buffers,
target and transfer counts, and transfer deadlines; a temporary requestor
window prevents stale same-target replies from crossing save generations.
Autostart fixtures verify GLib desktop-entry expansion, working directory,
TryExec, user overrides, visibility filters and the deliberate symlink policy.
The editor rereads the effective user override before updating enablement and
keeps write failures visible. There is one installed session defaults file,
and MAINTAINERS.md records that checked-in C is the release source. Python
remains only for the installed user-config migration command.

The optional apt-check now runs asynchronously with its 30-second process
limit. Guarded applications and window-manager recovery use capped exponential
backoff and reset after a healthy minute; launch logs no longer print argv.
The new override-source and restart-policy tests pass, and all 24 CTests passed
in a fresh Release build. CPack generated
`luxesession_0.5.5+gtk4-4+luxe50_amd64.deb`; payload inspection confirmed one
`desktop.conf` and no set-ID files. Luxe50 is current in `artifacts/debs`;
exact previous luxe49 is in `artifacts/old`. All 25 current/previous DEBs pass
their checksum manifests. Current SHA256:
`8f1580be19892b7b899428510f96de22f70884189f9c785def2ed7e94e4aa0a9`; previous
SHA256: `2310f3864432c6123065bd0550964ffb6c90fbaf74a5de702972a272239eed53`.
RPM generation is unavailable without `rpmbuild`. Audit progress is now 17/25
packages, with 105 resolved and 19 open/partial findings.

## 28 October 2026 — complete LuxeSettings audit, release luxe24

Closed SET-01 and SET-02. Missing settings programs remain disabled with a
clear reason, and an accessible header refresh button rebuilds availability on
demand. Each action declares whether it is a short-lived command that needs
asynchronous exit reporting or a detached GUI launch. The panel controller
reports nonzero exits without blocking GTK. Optional companion applications
remain optional in both package formats: DEB uses Recommendations and the
shared CPack helper now supports RPM weak Suggestions. The package release
number comes from one CMake revision variable.

Added and checked a German catalog containing 35 translations. Nested-X tests
verify German section/title/description labels without changing the action's
argv, provider refresh after executables appear or disappear, cached path use
after PATH changes, a failing controller's visible error, and shared dark/light
palette updates. All spacing now uses Luxe constants. Both CTests passed in a
fresh Release build. CPack generated
`luxesettings_0.1.0+luxe24_amd64.deb`; the payload contains the compiled German
catalog and no set-ID files or group-writable directories. Luxe24 is current
in `artifacts/debs`, exact previous luxe23 is in `artifacts/old`, and all 25
current/previous pairs plus manifest/checksum files validate. Current SHA256:
`06b7cbd04ea36ccd9966f513f6094a2ba6a6ec40f262d64d1952dda2a68ca627`; previous
SHA256: `375dcf36eda6d09d25e2599fcc7d7cd309ecb61777ce68b531087d2f240429cd`.
RPM weak suggestions are present in CPack configuration, but RPM generation
could not run because `rpmbuild` is unavailable. No host settings program was
launched. Audit progress is 16/25 packages, with 97 resolved and 27 open/partial
findings.


## 28 October 2026 — complete LuxeRandR audit, release luxe22

Closed RAND-01 through RAND-04. Apply and Save now compare a fresh bounded
query with the dialog's output and mode inventory; known external position
changes block use of a stale rollback layout. Save only records settings from
the last confirmed layout. The independent display guard reports actionable
reasons for a timeout, parent disconnect, failed process-group isolation or
rejected XRandR command, and reaps the command group before restoring. Generated
commands preserve exact positions for untouched outputs and safely quote valid
connector/mode names with spaces or quotes. Desktop Entry serialization is
separate from guard command quoting. Window recovery uses monitor rectangles
and runs only after Keep, so Revert leaves client windows where they were.

A fresh Release build passed all six CTests: guard lifecycle, process-group
failure, command construction, monitor-region placement, nested-X behavior and
query timeout. Regression coverage includes partial-apply restore, hangs,
parent disconnect, GUI death, changed known coordinates, unsafe values,
quoted mode names, staggered monitor gaps and window recovery timing. CPack
generated `luxerandr_0.3.2+gtk4-2+luxe22_amd64.deb`; the payload has no set-ID
files or group-writable directories. Luxe22 is current in `artifacts/debs`,
exact previous luxe21 is in `artifacts/old`, and all 25 current/previous pairs
and checksum manifests validate. Current SHA256:
`531c77263d396f44f2411fb5253fde6d84d08dc9a5efcf46d52d158c5be5ae82`; previous
SHA256: `222d5325f7676063a1bcd8366c450ab0a7f7694caa16d618acfc313a72d9c881`.
RPM packaging is unavailable because `rpmbuild` is not installed. No physical
display mode was changed. Audit progress is 15/25 packages, with 95 resolved
and 29 open/partial findings.


## 28 October 2026 — complete LuxePolkit audit, release luxe23

Closed POL-01 through POL-03. The identity selector remains limited to
Polkit-authorized choices and safely replaces the current session on selection.
X11 input acquisition is all-or-nothing; password input stays disabled until
both grabs succeed, with bounded retry and fail-closed cancellation. The broker
validates per-field, aggregate and item-count limits before serialization. Its
startup watchdog waits only for prompt readiness and secure grabs, then no longer
limits a legitimate password prompt. Timeout/cancel/crash paths reap the helper
and clear the busy gate. Debug logging stays free of cookies, details and
passwords. Both processes now share the theme-scheme helper, and all release
version definitions use one CMake revision.

A fresh Release build passed all four CTests: windowless agent registration,
broker child lifecycle, process boundary and dialog lifecycle. Tests include
alternate identity choice and stale-session completion, nested-X pointer and
keyboard contention/recovery/cancel, never-ready and no-prompt timeout followed
by a new request, oversized field rejection, sensitive debug-log checks and a
GTK-free broker parent. CPack generated
`luxepolkit_0.1.0+gtk4-5+luxe23_amd64.deb`; its payload has no set-ID files or
group-writable directories. Luxe23 is current in `artifacts/debs`, exact prior
luxe22 is in `artifacts/old`, and all 25 artifact pairs and manifest hashes
validate. Current SHA256: `0ecfea988d283b9d47e3ce44dd0c3b1e70e8f65ef5b1776c2b25b9e81a5e7058`;
previous SHA256: `7da40635ceab09bce38870ac99cbd9217478456d54c9377be726ea1a8ab117a5`.
RPM packaging is unavailable because `rpmbuild` is not installed. No host
authentication policy or live credential was used. Audit progress is 14/25
packages, with 91 resolved and 33 open/partial findings.


## 28 October 2026 — complete LuxePanel audit, release luxe64

Completed PAN-01 through PAN-07. Task icon reads and legacy pixmap geometry are
bounded and validated; the isolated X test now asserts rendered pixels, repeats
malformed/valid property changes, rejects oversized pixmaps and covers a vanished
window. Launcher drops keep bounded cancellable chunk reads and now test explicit
launcher-path and `text/uri-list` parsing, invalid payloads, size rejection and
exactly-once teardown completion. Tray message queues remain capped and expiring.
Directory menu enumeration, metadata checks and sorting moved to a worker; GTK
updates use a weak list reference and generation number. One scan worker is
allowed, with rapid navigation coalesced to the latest path. Run completion's
single-worker scheduling and scan/result caps were verified. Private D-Bus tests
cover notification count signals, stale replies and service owner replacement.
Clock boundary math and the shared startup desktop identity were reviewed; durable
config writes remain at explicit save boundaries.

A fresh Release build passed all 26 CTests. The focused directory-menu and
X-property tests also passed under ASan/UBSan with leak detection disabled for
GTK/Mesa/fontconfig process-global caches. CPack generated
`luxepanel_0.10.1+gtk4-8+luxe64_amd64.deb`; the payload audit found no set-ID
files or group-writable directories. Luxe64 is current in `artifacts/debs`, the
exact previous luxe63 is retained in `artifacts/old`, and all 25 current/previous
pairs plus manifest hashes were verified. Current SHA256:
`f2003fdda17853689008f4b5bd61e90c8251ef719fcc45ed5ecf52b94ea610e7`; previous
SHA256: `f9ab41cb9244f70ddee3f270d825b80d6a0fef2119afd41c37395cd089d03ed9`.
Optional RPM generation failed because `rpmbuild` is unavailable. No live panel
session was restarted. Audit progress is 13/25 packages, with 88 resolved and 36
open/partial findings.


## 28 October 2026 — complete LuxeNotify audit, release revision 15

Completed NOT-01 through NOT-04. Icon work is now limited to four concurrent
loads at 4 MiB of encoded data each; raw image hints share that byte ceiling
and reject dimensions above 2,048 pixels. Body, summary and action strings are
UTF-8 bounded, with a four-line body limit and 40-pixel decode target. Broker
method errors are returned to the caller without restarting a healthy child.
Timeouts and transport failures terminate the uncertain renderer, then wait
for reap before queued calls resume. The README and broker comments now define
renderer-owned card/count state versus the broker's cached count/active IDs.
Broker completion and recovery paths use readable blocks. Workarea updates
coalesce repositioning, and oldest cards are evicted when columns no longer
fit.

A fresh Release build passed all six CTests, including broker timeout/recovery,
remote rejection without restart, stable bus ownership, headless operation,
icon bounds/cancellation, and crowded workarea placement. CPack generated
`luxenotify_0.1.0-15_amd64.deb`; payload audit passed. Revision 15 is current in
`artifacts/debs`, exact previous revision 14 is in `artifacts/old`, and the
organizer audited/checksummed all 25 current/previous pairs. RPM packaging
could not run because `rpmbuild` is unavailable. No host notification service
was touched. Tests used private D-Bus and Xephyr sessions; physical monitor
hotplug and slow external icon sources were not exercised. Audit progress is
12/25 packages, with 81 resolved and 43 open/partial findings.

## 28 October 2026 — complete LuxeMenu Data audit, release luxe10

Completed MENU-01 and MENU-02. A single manifest maps each excluded Settings
desktop filename to its real package source entry. CMake generates the installed
menu exclusion list from that manifest, and the integration test reads the same
manifest and verifies the actual desktop files' categories. The DEB and RPM
metadata now require `luxesettings`, so excluded tools cannot be hidden when
the hub is absent. The private `menu-cache-gen` integration test always
registers, uses isolated XDG directories, and checks both the installed
`luxe-applications.menu` and legacy `lxde-applications.menu` symlink. It proves
the corresponding `luxe-applications-merged` and
`lxde-applications-merged` overrides load, while hub, store, third-party
Settings and terminal entries remain visible.

Fresh configuration and the menu-cache behavior test passed. CPack generated
`luxemenu-data_0.1.4+luxe-2+luxe10_all.deb`; the DEB payload audit passed and
metadata declares `Depends: luxesettings`. Luxe10 is current in
`artifacts/debs`, exact previous luxe9 is in `artifacts/old`, and the organizer
audited/checksummed all 25 current/previous pairs. RPM packaging could not run
because `rpmbuild` is unavailable. No host menu cache was touched. Audit
progress is 11/25 packages, with 77 resolved and 47 open/partial findings.

## 28 October 2026 — complete LuxeInput audit, release luxe13

Completed INP-01 through INP-03. On X11, the controls now initialize from the
effective pointer, repeat and bell settings while Cancel retains and restores
the exact original X values, including the acceleration fraction and bell
pitch/duration. The sensitivity threshold accepts zero; values displayed by
GTK are the values saved. Unsupported X11 and XKB controls are disabled.
Pointer and keyboard operations are checked after application, and a rejected
busy button mapping restores the previous checkbox and displays an error. The
optional keyboard-layout button stays visible with an explanation when
`lxkeymap` is absent. Session profile path components follow the
`DESKTOP_SESSION` contract, accept only safe ASCII names, map the Luxe alias to
LXDE, and safely fall back for malformed input. CMake now derives package
version metadata from one revision variable.

A fresh Release build passed the nested-X behavior test. It covers live control
initialization, exact Cancel restoration, zero-threshold persistence, missing
X11/XKB capabilities, injected MappingBusy rollback, and session-profile
validation. CPack generated `luxeinput_0.3.5-1+luxe13_amd64.deb`; payload audit
passed. Luxe13 is current in `artifacts/debs`, exact previous luxe12 is in
`artifacts/old`, and the organizer audited/checksummed all 25 current/previous
pairs. RPM packaging could not run because `rpmbuild` is unavailable. No host
input settings were changed. LuxeInput intentionally exposes X11 core controls;
Wayland/libinput device properties and physical hardware button-down behavior
were not tested. Audit progress is 10/25 packages, with 75 resolved and 49
open/partial findings.

## 28 October 2026 — complete LuxeHotkey audit, release luxe17

Completed HOT-01 through HOT-04. LuxeHotkey now shares the rc.xml transaction
lock with LuxeAppearance and ObConf across stale-baseline validation and atomic
publication, so suite writers cannot race the final comparison. External stale
content is rejected without overwriting it. Accelerator validation, collision
identity and Openbox serialization use the shared semantic parser. The CLI
keeps legacy `EXEC&OPTIONS`, supports escaped literal ampersands, and adds an
explicit `--options OPTIONS` syntax. GTK binding updates use one rollback policy
for app and global bindings, restoring a reused binding after an injected add
failure.

A fresh Release build passed all four CTests. Tests cover lock contention,
stale external edits, accelerator parsing/round trips, malformed options,
rollback and replacement behavior, malformed Openbox reload preservation, and
edit-dialog lifecycle. CPack generated
`luxehotkey_0.1.0+gtk4-2+luxe17_amd64.deb`; payload audit passed. Luxe17 is
current in `artifacts/debs`, exact previous luxe16 is in `artifacts/old`, and
the organizer audited/checksummed all 25 current/previous pairs. RPM packaging
could not run because `rpmbuild` is unavailable. No live shortcut configuration
was modified. Manual editors that ignore the advisory lock remain outside the
lock protocol; stale changes seen before commit are rejected. UI decision
branches were source-reviewed rather than automated as simulated clicks.
Audit progress is 9/25 packages, with 72 resolved and 52 open/partial findings.

## 28 October 2026 — complete LuxeDM audit, release luxe25

Completed DM-01 through DM-06 with implementation and isolated regression
evidence. The lock guard now follows actual nested-X root-size changes while
authentication blocks the UI thread and keeps its keyboard/pointer grabs through
handled signals. Logout cleanup detects live members of the session process
group, lets ordinary exits finish, then force-stops TERM-resistant descendants
without mistaking its unreaped zombie leader for a survivor or touching an
unrelated group. PAM conversation handling preserves a bound user identity
across echo-on factors, handles separate hidden prompts and informational-only
messages, clears password state, and wipes/frees partial responses on errors.
Selected desktop-file IDs and `DesktopNames` now drive the PAM desktop
environment; common legacy desktop commands are mapped to stable identities.
Application/DEB/RPM versions and the data path continue to derive from one
revision and CMake install prefix.

A fresh Release build passed all 15 CTests. Coverage includes nested-X lock
resize/signals, graceful and forced process-group cleanup, PAM conversation
failure and identity behavior, and two distinct desktop-file sessions plus a
legacy Openbox entry. A separate `/opt/luxe` configure embedded
`/opt/luxe/share/luxedm`. CPack generated
`luxedm_1.4.1-1+luxe25_amd64.deb`; payload audit passed. Luxe25 is current in
`artifacts/debs`, exact previous luxe24 is in `artifacts/old`, and the
organizer audited/checksummed all 25 current/previous pairs. RPM packaging was
attempted but cannot run without `rpmbuild`. No package was installed, no host
PAM stack or display manager was exercised, and physical monitor hotplug was
not tested. A process that deliberately escapes the session process group is
outside the process-group cleanup boundary. Audit progress is 8/25 packages,
with 68 resolved and 56 open/partial findings.

## 28 October 2026 — complete Luxe Appearance Openbox Configuration audit, release luxe16

Completed OBC-01 through OBC-06. Openbox theme installation now snapshots and
parses an archive once with the shared bounded tar reader, enforces compressed,
expanded, entry and path limits during extraction, stages privately, and
publishes the finished theme atomically without replacing an existing theme.
Closing the editor cancels the installer and removes its staging files. Archive
creation and theme discovery run off the GTK thread; archive creation bounds
tree depth, entry count, total input bytes and runtime, then uses atomic
no-replace hard-link publication. Theme discovery uses copied root paths,
bounded result collection, one sort and linear list assembly. Repeated model
selection and teardown during installation now have direct lifecycle coverage.
Tree getters no longer create missing XML nodes. Appearance and ObConf share a
private advisory lock while publishing rc.xml, and ObConf checks its loaded
file identity while holding that lock.

Fresh Release builds and all eight CTests across ObConf and Appearance passed.
Tests cover 4,100 archive
entries, a 257 MiB sparse-file compression bomb, corrupt/traversal/link/special
members, symlink theme roots, injected copy failure, cancellation on close,
duplicate destinations and two concurrent publishers with exactly one winner.
The private-X test repeats theme selection 1,000 times and confirms a canceled
old-window installer cannot update the reopened editor. The read-only getter
fixture proves missing nodes remain absent, and the lock test checks exclusion
and release. CPack produced `luxeappearance-obconf_2:0.2.3+gtk4-4+luxe16_amd64.deb`
and `luxeappearance_0.3.0+gtk4-2+luxe37_amd64.deb`; both payload audits passed.
Luxe16 and Luxe37 are current in `artifacts/debs`; exact previous luxe15 and
luxe36 are in `artifacts/old`. The organizer audited/checksummed all 25
current/previous pairs. RPM generation remains unavailable because `rpmbuild`
is not installed. No theme was installed to a user's home. Audit progress is
7/25 packages, with 65 resolved and 59 open/partial findings.

## 28 October 2026 — complete Luxe Appearance audit, release luxe36

Completed APP-01 through APP-07. The icon-theme installer now reads one
bounded archive stream through a no-follow descriptor, validates and extracts
with descriptor-relative paths, rejects unsupported PAX/GNU metadata clearly,
and bounds size, entries, path lengths, roots, and runtime. Themes publish with
atomic no-replace rename; concurrent installs have one winner, UI diagnostics
include bounded stderr, and window close terminates the single active job.
Openbox theme writes use a non-network XML parser, retain comments, protect
the opened file from symlinks, and preserve external edits on conflicts and
rollback. Wallpaper following uses Frame's saved source as the authority,
waits for Frame to apply and persist a requested image, and records success
only after verification. Theme enumeration/removal are asynchronous and
cancellable; unrelated edits reuse the active preset CSS provider. LuxePanel
restarts only for GTK theme changes because its controller has no live theme
reload command. DEB and RPM metadata both declare Frame.

The fresh Release build and all four CTests passed. Tests cover Openbox
comments and conflict preservation, wallpaper success/failure/manual override,
main-loop responsiveness during a delayed scan, concurrent theme publication,
malformed and oversized archives, newline filenames, special members,
deadline-return cleanup, and partial-copy cleanup. CPack produced
`luxeappearance_0.3.0+gtk4-2+luxe36_amd64.deb`; payload audit passed and the
binary's libxml2 runtime dependency appears once. Luxe36 is current in
`artifacts/debs`, exact previous luxe35 is in `artifacts/old`, and the
organizer audited/checksummed all 25 current/previous pairs. RPM generation
remains unavailable because `rpmbuild` is not installed. No user theme or
wallpaper was changed. Audit progress is 6/25 packages, with 62 resolved and
62 open/partial findings.

## 28 October 2026 — complete Luxe Icons audit, release revision 12

Completed ICO-01 through ICO-04. Icon generation now verifies that its output
is the canonical, marked build-owned theme tree, checks pinned archive hashes
before replacement, and stages the complete result before publishing it. A
single `icon-ownership.json` manifest drives fallback exclusions and coverage
expectations; package-level checks verify the real app packages in both install
orders and reject deliberate duplicate ownership. The configured icon-cache
hook follows the install data directory. Native C++17 tools now build in each
package build root using an explicit host compiler and exact executable paths;
test dependencies are scoped to their consumers, and cross builds require a
host compiler.

The clean Release build and all four CTests passed, including output
preservation, alternate-prefix cache hooks, coverage and nested-X lookup. A
separate build root independently built host tools; a simulated cross build
failed clearly without `LUXE_HOST_CXX_COMPILER` and succeeded with
`/usr/bin/c++`. Current `luxeterm`, `luxurystore`, `lucid`, and `luxup` DEBs
and candidate luxe12 passed both install orders, while a deliberately
conflicting DEB was rejected. CPack produced
`luxe-icons_2026.09.09-12_all.deb`; payload audit passed. Luxe12 is current in
`artifacts/debs`, exact previous luxe11 is in `artifacts/old`, and organizer
audit/checksum refresh passed for all 25 current and previous pairs. RPM
generation is unavailable because `rpmbuild` is not installed. Audit progress
is 5/25 packages, with 57 resolved and 67 open/partial findings.

## 28 October 2026 — complete Matinee audit, release luxe32

Completed MAT-01 through MAT-12. Media probing now runs off the GTK thread with
request generations and weak window references; FFmpeg probing has a bounded
interrupt deadline and explicit cancellation. Audio/video decode errors reach
the UI error state, and queued interleaved video packets are bounded to 64 MiB
and 512 packets. Library scans are cancellable workers that reconcile only
library-owned playlist rows. Seeking preserves the requested audio clock and
discards decoded audio and video preroll using timestamps. Spectrum work runs
only while a mapped audio-only visualizer needs it. Decoder submission errors
and queue-limit failures no longer disappear silently.

Release build and all four CTests passed. The behavior suite also passed under
ASan/UBSan with leak detection disabled for external GTK/FFmpeg process globals.
New coverage includes asynchronous playlist activation/teardown, A/V seeking,
the 64 MiB/512-packet queue limits, spectrum gating, pre-start cancellation,
delayed H.264 frames, sample-rate combinations, and audio/video drain ordering.
CPack produced `matinee_0.1.0-1+luxe32_amd64.deb`; the payload audit passed.
Luxe32 is current in `artifacts/debs`, exact previous luxe31 is in
`artifacts/old`, and organizer audit/checksum refresh passed for all 25 current
and previous pairs. Playback tests used a private ALSA null sink. RPM packaging
could not run because `rpmbuild` is unavailable. Audit progress is now 4/25
packages, with 51 resolved and 73 open/partial findings.

## 28 October 2026 — complete Lucid audit, release luxe22

Completed LUC-01 through LUC-06. Session recovery now reports exactly which
tabs and contents exceed its bounds and keeps one durable prior snapshot
before an incomplete overwrite, without allowing repeated limited writes to
grow an archive. Recovery writes are one-shot and cursor motion no longer
causes disk work. Hidden sidebar monitoring and irrelevant preference/modeline
work are avoided; style schemes use shared palette metadata. Named-session
reads and writes run off the GTK thread, with bounded incremental listing and
cancellation when the dialog closes. Sidebar file references and session
lifecycle ownership are covered by focused tests.

The fresh Release build and both CTests passed. CPack produced
`lucid_0.1.0-1+luxe22_amd64.deb`; payload audit passed. Luxe22 is current in
`artifacts/debs`, luxe21 is the sole prior in `artifacts/old`, and all 25
current/previous pairs passed organizer audit and checksum refresh. No package
was installed and no user document was opened. RPM generation was unavailable
because `rpmbuild` is not installed. At that point progress was 3/25 packages,
with 44 resolved and 80 open/partial findings.

## 28 October 2026 — correct package completion tally after Frame luxe33

Corrected the progress measure in `areport.md`: completion is package-level,
and a package is complete only when every finding in its section meets its
acceptance criteria or is explicitly closed with evidence. DebInstall and Frame
are fully complete, so the current tally is **2/25 packages complete** and 23
unfinished. Their DEB-01/04 and FRA-01 through FRA-07 findings have concrete
implementation and verification evidence. The finding-level tally is 39
resolved and 85 open/partial. The previously repeated 99 open count is stale.

## 28 October 2026 — complete Frame audit and promote luxe33

Closed FRA-01 through FRA-07. Frame now coalesces superseded image loads around
the shared bounded decoder, retains original image dimensions, prepares
wallpapers asynchronously, and rolls back root-pixmap publication when durable
settings writes fail. The folder chooser reports service errors and remains
available for correction. Wallpaper source paths, palette IDs/default and
release metadata now come from their respective single CMake authorities. The
palette catalog drives both LuxeSession theme generation and Frame; bundled
wallpaper asset filenames and checksums use those IDs directly. Unsafe palette
values cannot become filesystem paths. Bundled wallpapers follow the active
palette and keep daily/next-login rotation; personal choices remain unchanged.

The full Frame Release build and all eight CTests passed. This includes decode
limits, 8000×4000 source dimensions, desktop-entry file expansion, rapid-load
coalescing, async wallpaper work, save rollback, daily rotation and RandR
resize. LuxeSession's staged collection installer/hash regression passed after
its test fixture was updated to read the shared palette catalog rather than
repeat the old palette list. A non-default `/opt/luxe` configure embeds the
matching wallpaper data root. CPack generated
`frame_0.1.0-1+luxe33_amd64.deb`; the payload audit passed. Luxe33 is current in
`artifacts/debs`, luxe32 is the sole prior in `artifacts/old`, and all 25
current/previous DEB pairs passed organizer audit and checksum refresh. No
package was installed and no live desktop state was changed.

## 28 October 2026 — complete DebInstall audit, luxe16

Completed DEB-01 through DEB-04. Snapshot copies now enforce size, file
stability, digest and deadline checks at both GUI and privileged boundaries;
GUI copying is cancellable, inspector and package-file listing output are
bounded, and GDebi private API use is isolated behind an adapter. The audited
lifecycle code is easier to follow, and successful Close uses neutral styling.
RPM generation remains intentionally excluded for this Debian/APT-specific
installer, whose Python runtime remains required.

Fresh Release build and all 10 CTests passed, including copy-cancellation,
process-output limits, the GDebi adapter and privileged-copy deadline tests.
CPack generated `debinstall_0.1.0-16_all.deb`; payload audit passed. Luxe16 is
current and luxe15 is retained in `artifacts/old`. The organizer audited and
refreshed checksums for all 25 current/previous DEB pairs. No privileged
installation or live package operation was performed.

## 28 October 2026 — bound Frame decoder work during navigation

Frame now coalesces image requests while a codec call is running: it cancels
the active stream and retains only the newest requested file, then starts that
decode after the current codec call returns. This prevents rapid navigation
from stacking large, non-interruptible codec allocations. The frame behavior
regression opens two files before the main loop yields and verifies that the
latest file and its original dimensions win.

Decoder profiling on 8192×4096 fixtures scaled to 4096×2048 measured peak RSS
of 130,544 KiB (PNG), 171,404 KiB (GIF), 82,112 KiB (SVG), 32,720 KiB (JPEG),
and 40,752 KiB (WebP). Shared input and source-pixel limits remain 256 MiB and
64 megapixels; the measured values are format-specific working-set samples,
not a claim of a universal process-RSS ceiling. Focused Frame behavior and
decode-limit tests pass. Frame's full suite and package promotion remain
pending while its other audit findings are handled.

The same Frame behavior fixture now opens an 8000×4000 SVG and checks that the
viewer reports source dimensions while displaying a 4096×2048 preview; rotating
the preview must leave file dimensions unchanged. The behavior test passes.

Wallpaper directory enumeration, image decoding/scaling, pixel conversion and
PNG compression now run in a cancellable serialized worker. X11 publication
and settings writes stay on the main loop. A generation guard discards stale
work, a changed display size restarts preparation, scans cap at 4,096 entries,
and each request tries at most 32 candidates. The existing service command
waits in a nested main loop, so replacement/stop commands can still dispatch
and callers retain useful exit status. The wallpaper regression verifies
latest-request wins, timer progress during worker preparation and the 32-of-40
invalid-candidate failure summary. All eight Frame CTests passed; package-wide
build and promotion remain pending its other findings. Current audit tally:
38 findings resolved, 86 open/partial; package completion remains 1/25.

Wallpaper folder selection now waits asynchronously for the service result,
shows the captured failure, and leaves the chooser open for correction. If
durable settings cannot be written, the new X11 background is rolled back and
the previous selection and rotation schedule remain active. Regressions verify
that an empty folder returns failure without changing the persisted source,
and an unwritable settings path leaves the root pixmap unchanged. The focused
missing-operand, persistence-failure and wallpaper behavior tests pass.

## 28 October 2026 — test newline paths and FIFO rejection, LuxeAppearance luxe35

The archive installer regression now extracts a theme member whose filename
contains a newline, proving member names are not treated as line records. It
also rejects a FIFO archive member before publication. Release build and all
four CTests passed; the installer suite passed with ASan/UBSan-built archive
tools. The DEB payload audit passed. Luxe35 is current, luxe34 is retained,
and the organizer refreshed and verified all 25 current/previous artifact
pairs. No user theme was installed.

## 28 October 2026 — preserve the previous Luxe Icons output on failure, revision 10

Icon themes now build in a unique sibling staging directory. Generation runs
extraction, pruning, deduplication and coverage checks before moving the
finished tree into place; failed generation leaves the previous output
untouched, and a failed publication attempts to restore it. A new fixture
uses a checksum-valid invalid archive and verifies that the old tree and its
ownership marker survive. Fresh Release build and all four CTests passed; the
build deduplicated 22,454 SVGs. DEB payload audit passed, revision 10 is current
and revision 9 is retained. All 25 current/previous DEB pairs passed audit and
checksum verification.

## 28 October 2026 — expose detailed Matinee audio errors, luxe31

Matinee now retains ALSA playback and output/resampler initialization failures
and exposes a thread-safe copy to the window's playback tooltip. The behavior
test injects `EIO` and checks that the useful “Input/output error” detail is
preserved. Release application build, all four CTests, and the behavior suite
under ASan/UBSan passed. The DEB payload audit passed; luxe31 is current and
luxe30 is retained as the previous artifact. All 25 current/previous DEB pairs
were audited and checksums refreshed. No physical audio device was used; RPM
generation is unavailable without `rpmbuild`.

## 28 October 2026 — test partial display apply recovery, LuxeRandR luxe21

Added a fake xrandr case that records a partial Apply and then exits with
failure. The independent display guard must still restore and report
`FAILED_RESTORED`. CMake now derives app/test/DEB/RPM version strings from one
revision. Release build, all five CTests and DEB payload audit passed. Luxe21
is promoted; luxe20 is retained. No physical display mode changed. Optional
RPM generation could not run because `rpmbuild` is unavailable.

## 28 October 2026 — verify task-icon recovery after malformed data, luxe63

Extended the isolated-X production task-button test to replace a malformed
`_NET_WM_ICON` with a valid two-size property. It asserts the updater selects
the client property and leaves a paintable on the image after recovery.
Release build, all 26 CTests, focused ASan/UBSan test and DEB payload audit
passed. Luxe63 is promoted; luxe62 is retained. Optional RPM generation could
not run because `rpmbuild` is unavailable. No live panel session was touched.

## 28 October 2026 — exercise task-icon fallback, luxe62

The isolated-X property fixture now runs the production task-button icon update
with a malformed `_NET_WM_ICON`, unavailable WM_HINTS and an invalid legacy
pixmap. It asserts the real GtkImage receives the generic fallback paintable.
Release build, all 26 CTests, focused ASan/UBSan test and DEB payload audit
passed. Luxe62 is promoted; luxe61 is retained. RPM generation is unavailable
because `rpmbuild` is not installed. No live panel session was used.

## 28 October 2026 — strengthen Matinee's long-video-tail regression, luxe30

The generated A/V test now muxes 0.2 seconds of audio with a one-second video,
pauses/resumes after audio completion, and requires at least 0.85 seconds
elapsed plus 18 UI-delivered frames before playback finishes. This catches a
rushed video tail while allowing the UI to coalesce stale textures. Release
build, all four CTests, behavior tests under ASan/UBSan and DEB payload audit
passed. Luxe30 is promoted; luxe29 is retained. Fixtures use a private ALSA
null sink. `rpmbuild` is unavailable for optional RPM packaging.

## 28 October 2026 — verify negotiated Matinee duration and clock, luxe29

Expanded resampler/drain coverage across 15 combinations: 8, 22.05, 44.1, 48
and 96 kHz input at negotiated 32, 44.1 and 48 kHz output. Each 0.1-second
input must yield the expected accepted frame count and output clock. Release
build, all four CTests, behavior test under ASan/UBSan and DEB payload audit
passed. Luxe29 is promoted; luxe28 is retained. Tests use a private ALSA null
sink. Optional RPM generation could not run because `rpmbuild` is unavailable.

## 28 October 2026 — bound icon-theme archive extraction, luxe34

Replaced shell-driven tar listings and extraction with one C++17 streaming
extractor. It opens the archive without following symlinks, validates each
header while extracting to private staging, rejects links and unsafe/duplicate
paths, and uses descriptor-relative exclusive writes. Compressed size, expanded
file bytes, member count, path length and runtime are bounded. Gzip and bzip2
formats are both supported and checked through end-of-stream. Release build,
all four CTests, focused ASan/UBSan installer test and DEB payload audit passed.
Luxe34 is promoted; luxe33 is retained. The optional RPM phase could not run
because `rpmbuild` is unavailable. PAX/GNU long-name archives and the remaining
adversarial fixture matrix are still unsupported or unverified.

## 28 October 2026 — test pending launcher-drop teardown, luxe61

The launcher-drop lifetime regression now queues a real asynchronous
`GMemoryInputStream` read, destroys the plugin to cancel it, and dispatches the
production completion callback through GLib's main loop. This checks the
freed-plugin boundary and exactly-once drop completion instead of manually
calling the cleanup helper. Release build, all 26 CTests, focused ASan/UBSan
test and DEB payload audit passed. Luxe61 is promoted and luxe60 is retained.
The optional RPM phase could not run because `rpmbuild` is unavailable. MIME
success-path coverage, repeated/chunked cancellation, and backend
noncompletion remain open.

## 28 October 2026 — reject malformed trailing task icons, luxe60

The bounded `_NET_WM_ICON` parser now validates the complete property before
returning a candidate, including after an exact-size match. A truncated or
invalid later entry rejects the property instead of retaining an earlier
icon. Selection now prefers the requested size, then the largest pixel area.
Release build, all 26 CTests, focused ASan/UBSan property test and DEB payload
audit passed. Luxe60 is promoted and luxe59 is retained; no live panel ran.

## 28 October 2026 — verify Matinee resampling duration, luxe28

Added a synthetic resampler/drain check for 8, 22.05, 44.1, 48 and 96 kHz
inputs; each 0.1-second signal yields 4,800 frames at 48 kHz. The root-file
lifetime fixture now uses a non-media file to keep that ownership check
separate from GTK playlist teardown. Release build, all four CTests and DEB
payload audit passed. Luxe28 is promoted and luxe27 is retained. Tests use an
ALSA null sink only; no live playback ran.

## 28 October 2026 — fix Slab persistence fixtures, testing5

The window-model and Openbox tests now create an empty persisted pin list before
starting Slab. Previously they cleared only the in-memory list, so the
compare-before-write lock correctly rejected edits as stale against the saved
defaults. Release build, all five CTests and DEB payload audit passed. The
testing5 package is promoted; testing4 is retained as the previous artifact.
No live dock session or user pin data was changed.

## 28 October 2026 — unify Shotty package revision, luxe20

Shotty now defines its package revision once in CMake and derives the
application/DEB version and RPM release from it; the former independent RPM
metadata lagged one revision behind. Release build, the Xephyr-backed behavior
CTest and DEB payload audit passed. Luxe20 is promoted and luxe19 is retained
as the previous package. No real screen capture or viewer launch ran.

## 28 October 2026 — validate PCManager IPC ACKs and GNUInstallDirs paths, luxe42

Updated the v2 single-instance fixtures to use versioned socket names, split a
valid frame across writes, half-close and receive the positive ACK, and run a
concurrent ACK peer for sender verification. The forwarding fixture now uses
separate client and receiving option storage. PCManager and internal libfm
compile-time paths use GNUInstallDirs full paths, and CMake derives DEB/RPM
release values from one revision. Release build, all 10 CTests, `/opt/luxe`
path checks and DEB payload audit passed. No external file operation or live
desktop change ran.

## 28 October 2026 — correct LuxuryStore transaction-test expectations, 0.1.0-31

The backend test now expects the successful Close action to remain
non-destructive, and clears `GError` objects between expected helper failures
instead of triggering GLib's fatal warning. CMake derives DEB/RPM release
metadata from one revision. ABI6/ABI7 builds, all nine CTests and DEB payload
audit passed. Tests used simulated plans and local harmless commands; no
package operation or repository mutation ran.

## 28 October 2026 — repair LuxePower test setup and release metadata, 0.2.0-25

Fixed the boundary test's invalid GLib test path and gave the UI test its own
compiled package schema with the memory GSettings backend. The test had been
reading host schemas and failed to find `idle-lock-failed`. CMake now derives
the DEB version and RPM release from one revision. Release build, all four
CTests and DEB payload audit passed. No privileged or live power operation
ran.

## 28 October 2026 — derive LuxUp RPM release from its revision, luxe28

LuxUp now defines its package revision once in CMake and derives the
application/test/DEB version and RPM release from that value, removing a
metadata drift path. Release build, all three CTests and DEB payload audit
passed. No package refresh, install, Snap hold change or network update ran.

## 28 October 2026 — derive LuxeTerm RPM release from its revision, luxe19

LuxeTerm now defines its package revision once in CMake and derives the
application/DEB version and RPM release suffix from that value, preventing
the separate RPM release number from drifting. Release build, both isolated
CTest suites and DEB payload audit passed. No live terminal was opened.

## 28 October 2026 — single-source LuxeTask package revision, luxe17

LuxeTask now defines its package revision once in CMake and derives the
application/test version strings, DEB version and RPM release from that
revision. This prevents the binary's displayed version and package metadata
from drifting apart. Release configure/build and both CTests passed; the DEB
payload audit passed. No GUI was launched and no unrelated host processes were
signalled or reniced.

## 28 October 2026 — reconcile LuxeSession autostart filters and D-Bus paths, luxe49

Autostart enable/disable now updates desktop visibility lists idempotently and
computes checkbox state from both inclusion and exclusion filters, propagating
write failures. GNOME SessionManager client/inhibitor lifecycle signals now
treat object paths as strings, with a regression test for all four signals.
Compile-time install paths honor GNUInstallDirs, verified with an alternate
`/opt/luxe` prefix. Release build, all 23 CTests, alternate-prefix build and
DEB payload audit passed. No live session action ran; RPM packaging is
unavailable without `rpmbuild`.

## 28 October 2026 — launch the resolved LuxeSettings executable, luxe23

Settings action rows now retain the absolute executable that was found when
the row was created and launch that exact file. Changing `PATH` after the
availability check can no longer redirect an enabled action or make it fail
to resolve. The UI test target now links the shared GTK helper library it
uses. Release build, both CTests, and DEB payload audit passed. The nested-X
UI test launches private fixtures after changing `PATH` to `/nonexistent`;
no host settings tool ran.

## 28 October 2026 — require private process group for RandR commands, luxe20

The independent display rollback guard now verifies that every xrandr child
uses its own process group before starting the command. It handles the
parent/child `setpgid()` race by checking the child's actual group; if no
private group can be confirmed, it kills and reaps the direct child without
running xrandr. A fault-injected regression forces group creation to fail and
checks that Apply/Restore are never invoked. Release build, all five isolated
CTests and DEB payload audit passed. No physical display was changed.

## 28 October 2026 — report Polkit ready only after a protected prompt, luxe22

The dialog child now signals readiness only after Polkit supplies the prompt
and both X11 input grabs succeed. This makes the broker startup deadline
detect a child that launched but never produced a usable prompt, while leaving
user password-entry time unbounded. Added a no-prompt fixture that verifies
timeout, busy-state cleanup and successful processing of the next request.
Release build, all four isolated CTests, and DEB payload audit passed. CPack
produced the DEB; RPM packaging was unavailable because `rpmbuild` is absent.
No package was installed and no live authorization policy was exercised.

## 28 October 2026 — fix launcher-drop path double release, luxe59

Successful launcher-drop completion transferred an `FmPath` to the cleanup
function, which already unrefs it, then unrefed it again in the caller. The
second release is removed. Release build and DEB payload audit passed. Four
focused tests passed: launcher-drop lifetime, tray message limits, X property
boundaries, and taskbar overflow. The lifetime test does not exercise the
successful path, so a direct double-release regression, real MIME transfer,
and sanitizer teardown coverage remain open.

## 28 October 2026 — share the LuxeNotify D-Bus contract, luxe14

The broker and GTK renderer now share one GTK-free header for notification and
Luxe control introspection, bus/interface names, capabilities, and server
metadata. This prevents the two D-Bus endpoints from drifting independently.
Release build (including test-binary compilation) and DEB payload audit passed;
tests and bus ownership were not run. Broker state/lifecycle ownership and
ordering coverage remain open.

## 28 October 2026 — keep LuxeMenu integration test visible, luxe9

CTest now always contains the named menu-layout integration test. It accepts
only the private Luxe menu-cache generator; if that executable is unavailable,
CTest marks the test skipped instead of silently omitting it or using an
arbitrary system generator. Release configuration/build, test-inventory
inspection and DEB payload audit passed. Menu integration was not run; installed
merge-directory behavior remains open.

## 28 October 2026 — persist displayed LuxeInput values, luxe11

After GTK initializes the range controls, LuxeInput now stores their actual
displayed values back into the configuration fields used by Save. Out-of-range
settings therefore cannot silently remain different in memory than the
clamped values the user sees. Release build and DEB payload audit passed;
tests and live X input changes were not run. Effective-state reconciliation
and zero-threshold policy remain open.

## 28 October 2026 — protect LuxeHotkey edits on reload and close, luxe15

Reload and window-close requests now ask whether to Save, Discard or Cancel
when applied shortcut edits are pending. Failed saves keep the action open.
Reload parses a replacement before discarding the current model; if loading
fails after Discard was chosen, the old edits remain and Save is re-enabled.
Unapplied drafts in the separate edit window are not yet part of dirty
tracking. Release build and DEB payload audit passed. GTK decision-flow tests
were not run; stale external-file conflicts remain tracked under HOT-01.

## 28 October 2026 — support Unicode PAM input in LuxeDM, luxe24

Greeter and locker input now use XIM UTF-8 lookup where available, validate
codepoints, reject controls and enforce byte limits. Backspace removes a whole
Unicode codepoint; hidden-password bullets correspond to codepoints. ASCII
XLookupString remains the fallback without an input method. Release build,
test-binary compilation and DEB payload audit passed; tests/authentication were
not run. Nested-X composition and limit coverage remain open; RPM packaging was
unavailable because `rpmbuild` is not installed.

## 28 October 2026 — retain LuxeDM session-group identity through cleanup, luxe23

The session leader is observed with `waitid(WNOWAIT)` and remains unreaped
until bounded process-group shutdown completes. This keeps its numeric PGID
reserved while `killpg` stops surviving descendants, preventing a recycled
group ID from targeting unrelated processes. Build and DEB payload audit
passed; tests and real logout operations were not run. Escaped descendants and
dedicated lifecycle coverage remain open.

## 28 October 2026 — declare LuxeDM RPM command providers, luxe22

RPM requirements now include `polkit` for pkexec and `xrandr` for the display
setup hook. Fedora package metadata confirms those package/file mappings;
Debian already receives xrandr through `x11-xserver-utils`. Release build and
DEB payload audit passed. Tests were compiled but not run; RPM generation was
unavailable because `rpmbuild` is not installed. Other RPM cohorts still need
dependency-resolution validation.

## 28 October 2026 — preserve selected desktop identity in LuxeDM, luxe21

The session catalog now carries desktop-file ID and `DesktopNames` separately
from display label and Exec. PAM receives environment values for the selected
desktop instead of always identifying every login as Luxe. Core fixtures
assert the structured identity. Release build and DEB payload audit passed;
tests and session launches were not run. RPM packaging was unavailable because
`rpmbuild` is not installed.

## 28 October 2026 — present individual PAM prompts in LuxeDM, luxe20

The greeter and locker now display PAM prompt and information/error text. The
locker skips password input for info-only batches and asks separately for each
hidden prompt, clearing the entered password after creating its response.
Release build, PAM test-binary compilation and DEB payload audit passed; tests
and live authentication/locking were not run. Unicode input and behavioral
conversation tests remain open; RPM packaging was unavailable because
`rpmbuild` is not installed.

## 28 October 2026 — unify LuxeDM package metadata, luxe19

One CMake revision now generates the LuxeDM application, Debian and RPM
versions. The compiled theme-data directory follows
`CMAKE_INSTALL_FULL_DATADIR`, including configured nondefault prefixes, rather
than hardcoding `/usr/share`. Release build and DEB payload audit passed; test
binaries were compiled but not run. RPM packaging was unavailable because
`rpmbuild` is not installed.

## 28 October 2026 — harden LuxePower TLP conflict reads, 0.2.0-24

The privileged helper now requires the existing TLP configuration to be a
root-owned regular file that is not group- or world-writable before comparing
the reviewed digest or replacing it. This supplements the bounded no-follow
read and in-read identity/mode stability checks. Build and DEB payload audit
passed; tests and privileged operations were not run. External-writer races
and rollback failure injection remain open.

## 28 October 2026 — surface idle-lock failures in LuxePower, 0.2.0-23

The idle monitor now stores lock-readiness failure in the shared GSettings
schema and clears it after a successful lock. The Display page watches this
state and shows a warning that the screen may not be locked. Build, test-binary
compilation and DEB payload audit passed; tests and live lock operations were
not run. Cross-process settings propagation remains to be integration-tested;
RPM packaging was unavailable because `rpmbuild` is not installed.

## 28 October 2026 — detect TLP conflicts and recover activation failures, 0.2.0-22

Apply now carries the reviewed configuration digest. The privileged helper
compares it with the current no-follow, bounded read under a transaction lock
and rejects stale edits. If `tlp start` fails after saving, it restores the
captured original content and mode and retries activation, reporting whether
recovery was confirmed. Tests and privileged operations were not run; build
and DEB payload audit passed. External-writer race and failure-injection
coverage remain open. RPM packaging was unavailable because `rpmbuild` is not
installed.

## 28 October 2026 — transact LuxePower sleep-mask changes, 0.2.0-21

Sleep settings now take a root-owned transaction lock and snapshot persistent
and runtime systemd masks before applying changes. A failed command triggers
restoration of the affected and preceding targets, with a distinct message if
rollback is incomplete. The UI explains that enabling a path removes existing
administrator masks. A disposable mask-scope regression case was compiled;
tests and privileged operations were not run. DEB payload audit passed; RPM
packaging was unavailable because `rpmbuild` is not installed.

## 28 October 2026 — run LuxePower idle fallback asynchronously, 0.2.0-20

The `xprintidle` fallback now runs in a worker through the shared 15-second,
output-bounded process-group runner instead of blocking the monitor's GTK loop.
Only one query is outstanding; results do not trigger a lock after locking has
been disabled. Startup checks executable availability without running it
synchronously. Build, idle-test compilation and DEB payload audit passed;
tests and live lock operations were not run. RPM packaging was unavailable
because `rpmbuild` is not installed.

## 28 October 2026 — bound LuxePower subprocess probes, 0.2.0-19

The shared runner now drains stdout and stderr together, retains at most 1 MiB
per stream, enforces a 15-second timeout, and terminates the helper's process
group on timeout or excessive output. TLP configuration probes now share the
same bounded path as status refresh. An output-limit regression case was added
and compiled, but tests were not run. The synchronous `xprintidle` fallback
remains open work. DEB payload audit passed; RPM packaging was unavailable
because `rpmbuild` is not installed.

## 28 October 2026 — detect LuxePower masks in both systemd scopes, 0.2.0-17

Sleep-target state discovery now checks persistent and runtime systemd mask
locations independently. An ordinary unit or non-mask symlink in `/etc` no
longer hides a runtime mask in `/run` (and vice versa). Release build and DEB
payload audit passed; tests were compiled but not run, and no systemd target was
changed. RPM packaging was unavailable because `rpmbuild` is not installed.

## 28 October 2026 — preserve LuxePower edits after failed apply, 0.2.0-16

When privileged TLP configuration application fails, LuxePower no longer
reloads automatically and discards the user's edits. The visible values remain
for review; editing and Apply are disabled because the configuration may
already have been written, while Discard explicitly reloads current disk state.
Release build and DEB payload audit passed; tests and privileged operations
were not run. RPM packaging was unavailable because `rpmbuild` is not installed.

## 28 October 2026 — serialize LuxePower sleep and lid UI edits, 0.2.0-15

Sleep and lid controls are disabled while pkexec applies a change, then
re-enabled and refreshed from observed system state on completion. This
prevents rapid repeated edits from appearing applied before the privileged
operation returns. Mask ownership and rollback remain open audit work. Release
build and DEB payload audit passed; tests and privileged operations were not
run. RPM packaging was unavailable because `rpmbuild` is not installed.

## 28 October 2026 — frame PCManager single-instance IPC, luxe41

Forwarded requests now use a versioned socket, explicit frame markers and
data-prefixed argument lines. The receiver parses only a complete cleanly
closed request and acknowledges valid arguments before invoking the callback;
the sender reports forwarding success only after that acknowledgement.
Release build and DEB payload audit passed; IPC tests were compiled but not
run. RPM packaging was unavailable because `rpmbuild` is not installed.

## 28 October 2026 — bound LuxePanel launcher-drop reads, luxe58

Launcher and URI drops now stream in bounded chunks and reject payloads above
256 KiB before accumulating excess data. The callback checks cancellation
before accessing plugin state after asynchronous completion. The drop lifetime
test was adjusted to the stream parser and its size-bound helper. Build and DEB
payload audit passed; tests were compiled but not run. RPM packaging was
unavailable because `rpmbuild` is not installed.

## 28 October 2026 — bound and deduplicate Matinee library scans, luxe27

Library scans now cap visited directory entries and accepted files, filter
common media extensions, and avoid re-adding paths already present using a
hash set. Scanning still runs on GTK and silently truncates on limits/errors.
Release build and DEB payload audit passed; test binaries compiled but tests
and playback were not run.

## 28 October 2026 — report LuxeSession autostart save failures, luxe48

Autostart edits now propagate source-read and atomic-write errors. Failed toggles
restore the prior enabled state, failed system-entry removal is reported, and
the editor remains open after an autostart save failure. Earlier rows may still
be persisted before a later row fails. Build, DEB payload audit and artifact
checks passed; tests were compiled but not run. RPM packaging was unavailable
because `rpmbuild` is not installed.

## 28 October 2026 — bound LuxeDM session-group shutdown, luxe18

Logout now gives the dedicated session process group two seconds after HUP and
TERM, then sends KILL if the group remains. Signal failures are logged. A
logind/cgroup scope is still needed to reliably identify escaped descendants
and eliminate process-group reuse concerns. Release build and DEB payload audit
passed; tests were compiled but not run. RPM packaging was unavailable because
`rpmbuild` is not installed.

## 28 October 2026 — prevent concurrent Slab pin-list overwrites, testing4

Dock and Settings now lock pin updates, reload the current list under the lock,
and save only when it still matches the list used to prepare the edit. Stale
updates return a conflict and preserve disk and memory state. Automatic view
refresh and retry-free conflict handling remain open. Release build and DEB
packaging passed; test binaries compiled but tests and live UI actions were not
run.

## 28 October 2026 — report PCManager copy cleanup failures, luxe40

PCManager now checks recursive no-follow staging cleanup and reports the
retained temporary path and cleanup cause when a failed copy cannot remove its
partial tree. Cleanup deadlines and metadata-copy errors remain open. Release
build, DEB packaging and payload audit passed; tests and file operations were
not run.

## 28 October 2026 — cancel superseded LuxuryStore searches, 0.1.0-30

Changing a store query, source or reload now cancels the previous request for
that source and terminates/reaps its isolated subprocess group. Generation
checks continue to prevent stale results from replacing current rows. Output
limits, catalog-wide deadlines and closing during searches remain outstanding.
Release build, DEB packaging and payload audit passed; tests and package-manager
commands were not run.

## 28 October 2026 — expose per-battery TLP thresholds, 0.2.0-14

LuxePower now discovers batteries with both charge-threshold interfaces and
builds controls from each device's matching TLP settings, including BAT1-only
systems. Release build, DEB packaging and native payload audit passed. Test
binaries were built but not run; no privileged or live power operation occurred.

## 28 October 2026 — validate the LuxUp Snap policy marker, luxe27

LuxUp now refuses to apply or remove a Snap hold when its ownership marker is
malformed, preserving the marker for recovery instead of trusting file
existence alone. Release build, DEB packaging and native payload audit passed.
Tests and package-manager actions were not run.

## 28 October 2026 — identify stale CPU data after task scan failure, luxe16

When /proc enumeration fails, LuxeTask now preserves the last task rows but
clears the CPU bar and marks usage unknown instead of showing an old percentage
as current. Release build, DEB packaging and native payload audit passed. Tests
and GUI were not run.

## 28 October 2026 — confirm LuxeTask priority changes, luxe15

Priority changes now require confirmation that identifies the process, PID and
requested priority. The callback revalidates the selected process identity
before changing it; Linux's numeric-PID setpriority race remains documented.
Release build, DEB packaging and native payload audit passed. No tests or GUI
were run; no process was controlled.

## 28 October 2026 — skip LuxeTask sampling while minimized, luxe14

LuxeTask now pauses `/proc` collection and username lookups while its window is
minimized, while leaving the refresh timer active. The DEB candidate was built
without installation; tests and GUI were not run.

## 28 October 2026 — add the Luxe Settings translation template, luxe22

Added a POTFILES inventory and UTF-8 gettext template for Luxe Settings' marked
messages. No language catalogs exist yet. The DEB candidate was built without
installation; tests and a GUI were not run.

## 28 October 2026 — localize Luxe Settings About text, luxe21

The About dialog's program name and description now use the gettext domain.
The DEB candidate was built without installation; tests and a GUI were not run.
No translated catalogs are available yet.

## 28 October 2026 — use shared theme synchronization in Luxe Settings, luxe20

Luxe Settings now uses the shared GTK initializer for color-scheme updates and
removes its duplicate settings subscription. The DEB candidate was built
without installation; tests and GUI were not run.

## 28 October 2026 — initialize gettext in Luxe Settings, luxe19

Luxe Settings now marks its catalog and primary hub labels for translation,
initializes the gettext domain and compiles catalogs when present. No `.po`
catalogs currently exist, so this adds translation support without providing
translated messages. The DEB candidate was built without installation; tests
and a GUI were not run.

## 28 October 2026 — remove unused title-markup code from Luxe Settings, luxe17

Removed an escaped-title helper and its Pango test because the application uses
plain GTK labels. The DEB candidate was built without installation; tests and a
GUI were not run.

## 28 October 2026 — use shared outer spacing in Luxe Settings, luxe16

The Luxe Settings page now uses the shared outer-margin constant instead of a
separate 14px CSS padding value. The DEB candidate was built without
installation; tests and GUI were not run.

## 28 October 2026 — theme the LuxePanel notification badge, luxe57

The notification count badge now uses GTK's selected background and foreground
colors instead of fixed amber/dark values. The DEB candidate was built without
installation; tests and a live panel session were not run.

## 28 October 2026 — cap LuxePanel Run completion scans, luxe56

Run completion now scans at most 32,768 PATH entries and keeps at most 2,048
suggestions. The DEB candidate was built without installation; tests and a live
panel session were not run. A blocked filesystem operation can still delay the
single worker.

## 28 October 2026 — bound LuxePanel directory menus, luxe55

LuxePanel now scans no more than 4096 directory entries and renders at most 256
subdirectories in a directory menu, with a localized notice when results are
omitted. The DEB candidate was built without installation; tests and a live
panel session were not run. The scan still runs on the panel thread.

## 28 October 2026 — cancel DebInstall package inspection on close, luxe15

Closing DebInstall now terminates the unprivileged inspection process group and
reaps the inspector. Inspection also retains its 120-second deadline. The DEB
candidate was built without installation; tests and privileged operations were
not run. Captured inspector output still needs an explicit size limit.

## 28 October 2026 — reuse the RandR query token splitter, LuxeRandR luxe19

LuxeRandR now compiles the whitespace-token parser once for each display query
instead of once for every line. The DEB candidate was built without installation;
tests and display changes were not run. Mode-name serialization remains open.

## 28 October 2026 — stop logging PolicyKit action identifiers, LuxePolkit luxe21

LuxePolkit no longer logs the PolicyKit action identifier from the authentication
dialog listener in debug builds. The DEB candidate was built without installation;
tests and authentication were not run. RPM generation remains unavailable because
`rpmbuild` is not installed.

## 28 October 2026 — report panel-controller failures, LuxeSettings luxe15

Luxe Settings now waits asynchronously for the short-lived panel configuration
command and shows an error if it exits unsuccessfully. Other settings launchers
remain detached. The DEB candidate was built without installation; tests and
panel configuration were not run.

## 28 October 2026 — reject missing wallpaper command operands, Frame luxe32

Frame now returns a clear nonzero error when `--set-wallpaper` or
`--wallpaper-directory` is supplied without a path, instead of silently applying
the prior wallpaper state. The DEB candidate was built without installation;
wallpaper settings and tests were not run.

## 28 October 2026 — cancel package copy when DebInstall closes, luxe14

Closing DebInstall now signals the active snapshot-copy worker, which checks the
request between 1 MiB chunks and removes its partial snapshot on cancellation.
The DEB candidate was built without installation; runtime behavior was not
tested. A filesystem read already in progress and the inspection subprocess
still require separate cancellation work.

## 28 October 2026 — style successful DebInstall Close neutrally, luxe13

DebInstall's successful completion button is now styled as an ordinary Close
action rather than a destructive action. The UI assertion now checks that
semantic state. The DEB candidate was built without installation; tests were
not run.

## 28 October 2026 — close package snapshot descriptors once, DebInstall luxe12

DebInstall's bounded snapshot helper now explicitly transfers descriptor
ownership to `fdopen()` and only closes the raw descriptor if that transfer
fails. This prevents error cleanup from accidentally closing a reused file
descriptor. The DEB candidate was built without installation; runtime tests
were not run.

## 28 October 2026 — bound package snapshot copies, DebInstall luxe11

DebInstall now caps local and privileged package snapshots at 2 GiB, copies only
the initial file length, detects growth/truncation or concurrent metadata
changes, and verifies the reviewed checksum in the privileged helper. The
existing checksum and ownership checks remain. The DEB candidate was built
without installation; no privileged operation or runtime test was run.

## 28 October 2026 — packaging fix: omit unsupported DebInstall RPM, luxe10

DebInstall now intentionally produces DEB packages only. Its supported runtime
depends on Debian's APT/GDebi stack, which the prior generated RPM did not
describe. The Python application runtime and DEB dependencies are unchanged.
The DEB candidate was built without installation; no privileged operation was run.

## 28 October 2026 — audit cleanup: sample page size once, LuxeTask luxe13

LuxeTask now obtains the system page size once before its first process sample,
instead of repeating the same system query on application activation. The DEB
candidate was built without installation; runtime behavior was not tested.

## 28 October 2026 — audit fix: mark unavailable settings tools, LuxeSettings luxe14

Luxe Settings now disables catalog entries whose executable is not installed and
shows a concise availability state. The DEB candidate was built without
installation; runtime behavior was not tested.

## 28 October 2026 — audit fix: report directory menu open errors, LuxePanel luxe54

LuxePanel's directory menu now shows an error row when its configured folder
cannot be opened, instead of presenting the failure as an empty directory. The
DEB candidate was built without installation; runtime behavior was not tested.

> User-facing change history, newest first. Versions named in an entry are the
> versions built at that time; the current package set is in
> [`LUXE-BIBLE.md`](LUXE-BIBLE.md) and `artifacts/manifest.json`.

## 28 October 2026 — audit fix: detect stale Openbox shortcut edits, LuxeHotkey luxe14

LuxeHotkey now compares the current Openbox file to the version loaded by the
editor before saving. Observed external changes produce a conflict instead of
being overwritten. The DEB candidate was built without installation; Openbox
was not reconfigured.

## 28 October 2026 — audit fix: wipe partial PAM responses, LuxeDM luxe17

LuxeDM now overwrites every allocated PAM response before freeing it when a
later prompt fails. The DEB candidate was built without installation; no
authentication attempt was made.

## 28 October 2026 — audit fix: honor Desktop Entry autostart semantics, LuxeSession luxe47

The autostart runner now uses GLib's desktop-app launcher to expand and launch
entries instead of treating their `Exec` value as a shell command. The DEB
candidate was built without installation; no user autostart entries were run.

## 28 October 2026 — audit fix: disable unsupported live input controls, LuxeInput luxe10

LuxeInput now disables X11-only live controls on non-X11 displays and explains
the backend requirement. The DEB candidate was built without installation; no
live input settings were changed.

## 28 October 2026 — audit fix: clarify keyboard settings, LuxeSettings luxe13

The hub now describes LuxeInput's current pointer, key-repeat and sound
controls accurately, with keyboard-layout editing labeled optional. The DEB
candidate was built without installation; runtime behavior was not exercised.

## 28 October 2026 — audit fix: neutral successful transaction Close, LuxuryStore 0.1.0-29

LuxuryStore no longer styles the Close button as destructive after a successful
package transaction. The DEB candidate was built without installation; no
package operation was run.

## 28 October 2026 — audit fix: verify readiness for idle locking, LuxePower 0.2.0-13

LuxePower's idle locker now uses the same bounded readiness handshake as the
manual Lock action. The DEB candidate was built without installation; no lock
was invoked on the active session.

## 28 October 2026 — audit fix: validate Slab workarea geometry, testing3

Slab now rejects malformed or out-of-root workarea rectangles and falls back
to root geometry. The experimental DEB candidate was built without
installation; no live dock state was changed.

## 28 October 2026 — audit fix: sync staged screenshot data, Shotty luxe19

Shotty now syncs a completed staged PNG before replacing its destination. A
sync error keeps the previous file intact. The DEB candidate was built without
installation; no screenshot was captured in this step.

## 28 October 2026 — audit fix: report unsupported legacy wallpaper modes, PCManager luxe39

PCManager now forwards the supported `stretch` mode to Frame and rejects
unsupported legacy wallpaper modes with a clear diagnostic. The DEB candidate
was built without installation; runtime wallpaper behavior was not exercised.

## 28 October 2026 — audit fix: preserve notification CLI arguments, LuxeNotify 0.1.0-13

The resident broker now forwards every command-line argument to its renderer
instead of silently dropping all but the first. The DEB candidate was built
without installation; runtime behavior was not exercised.

## 28 October 2026 — audit fix: persist APT refresh freshness, LuxUp luxe26

LuxUp now retains its last successful APT list refresh time across service and
desktop restarts using a bounded, atomic per-user state record. The DEB
candidate was built without installation; runtime behavior was not exercised.

## 28 October 2026 — audit fix: avoid unrelated panel restarts, LuxeAppearance luxe33

Applying font, cursor, icon or toolbar settings no longer restarts LuxePanel.
The restart now occurs only when the GTK theme changed. The DEB candidate was
built without installation; runtime panel behavior remains unverified.

## 28 October 2026 — audit fix: preserve seek position in audio clock, Matinee luxe26

Audio clock updates now include the requested seek position, preventing the
first audio write from snapping the playback position back to zero. The DEB
candidate was built without installation; end-to-end seek behavior remains
unverified.

## 28 October 2026 — audit fix: preserve windows during display trials, LuxeRandR luxe18

Display trials no longer request moves for other windows before the new layout
is kept or after it is reverted. Four isolated tests pass, and the DEB candidate
was built without installation. Monitor-aware recovery remains to be addressed.

## 28 October 2026 — audit fix: responsive display queries, LuxeRandR luxe17

LuxeRandR now queries display topology asynchronously and cancels pending work
when its window closes, keeping the interface responsive during slow queries.
All four isolated tests pass and the DEB candidate was built without installation.
Real display-mode changes still need validation.

## 28 October 2026 — audit fix: bound display-query output, LuxeRandR luxe16

LuxeRandR now rejects topology-query output larger than 1 MiB and reaps the
timed command. Four isolated tests pass; the DEB candidate was built without
installation. Moving the query off the GTK thread remains open.

## 28 October 2026 — audit fix: independent display rollback, LuxeRandR luxe15

LuxeRandR now applies display changes through a separate timed guard that
restores the previous layout if confirmation times out or the settings
process exits. Four isolated tests pass, including a killed parent and hung
fake command; the DEB candidate was built without installation. Real mode
changes and remaining query limits still need validation.

## 28 October 2026 — audit fix: bound display queries, LuxeRandR luxe14

LuxeRandR now times out a stuck topology query instead of waiting indefinitely.
All three isolated tests pass; the DEB candidate was built without installation.
Independent Apply/rollback protection remains open.

## 28 October 2026 — audit fix: external Cut and Copy, PCManager luxe38

PCManager now honors the standard external file clipboard's Cut or Copy action
using a bounded asynchronous read. Clipboard owner changes cancel the pending
paste, and Trash still asks for confirmation. All ten isolated tests pass,
including a separate clipboard-owner process; the DEB candidate was built
without installation. Partial-batch clipboard reconciliation remains open.

## 28 October 2026 — audit fix: update-service handoff, LuxUp luxe25

If the update service starts while LuxUp is using its local fallback, the UI
now adopts the service and discards the cancelled local result. A forwarded
refresh during a service check is routed to the service. Three isolated tests
pass; the DEB candidate was built without installation. Duplicate checker
implementation and further transition tests remain open.

## 28 October 2026 — audit fix: update-service reconnection, LuxUp luxe24

LuxUp now clears stale results on service loss and requests fresh state when
the service returns. Three isolated tests pass, including owner changes and
late replies; the DEB candidate was built without installation. Initial
service absence and duplicate fallback checker ownership remain open.

## 28 October 2026 — audit fix: asynchronous update-service startup, LuxUp luxe23

LuxUp now opens its window while connecting to the update service and waiting
for initial state. Three isolated tests pass, including a delayed startup
reply; the DEB candidate was built without installation. Service reconnection
and checker ownership remain open.

## 28 October 2026 — audit fix: asynchronous update-service checks, LuxUp luxe22

Check and Cancel requests to the resident update service no longer block the
window while awaiting a reply. Three isolated tests pass, including a withheld
service reply; the DEB candidate was built without installation. Startup IPC
and service reconnection remain open.

## 28 October 2026 — audit fix: reduce raw icon copying, LuxeNotify 0.1.0-12

LuxeNotify now scales raw notification images without first duplicating the
entire pixel payload. Six isolated tests pass, including a texture-lifetime
check; the DEB candidate was built without installation. Decoder and message
memory limits remain open.

## 28 October 2026 — audit fix: bounded icon reads, LuxeNotify 0.1.0-11

Notification icons now use asynchronous opened-file metadata and bounded
reads, rejecting encoded files over 16 MiB even if they grow after inspection.
All six isolated tests pass; the DEB candidate was built without installation.
Decoder intermediate memory and slow-backend cancellation remain open.

## 28 October 2026 — audit fix: crowded notification placement, LuxeNotify 0.1.0-10

When notification columns fill the usable screen, LuxeNotify now closes older
cards instead of placing them beyond the left edge. Six isolated tests pass;
the DEB candidate was built without installation. Oversized single cards and
real hotplug remain open.

## 28 October 2026 — audit fix: missing APT backend, LuxUp luxe21

LuxUp skips APT commands when APT is absent, keeps other available results and
explains that native system updates were not checked. The tray remains unknown
for an empty result. All three isolated tests pass; the DEB candidate was
built without installation. Other native package backends remain open.

## 28 October 2026 — audit fix: bound Openbox configuration reads, LuxeAppearance luxe32

Appearance now reads Openbox configuration with a 1 MiB byte limit and checks
the opened file's version before and after reading. All four isolated tests
pass; the DEB candidate was built without installation. XML-aware matching
and no-follow opening remain open. The handbook's incorrect LuxeRandR version
was corrected to luxe13.

## 28 October 2026 — audit fix: identify failed update sources, LuxUp luxe20

LuxUp now names failed APT, Flatpak user/system and Snap checks in its window
and command-line output. The command-line Snap policy status also reports
unknown when its query fails. All three isolated tests pass; the DEB candidate
was built without installation. Per-source freshness remains open.

## 28 October 2026 — audit fix: handle failed Stop rewind, Matinee luxe25

Matinee now keeps the last known position and enters an error state when Stop
cannot rewind; Play does not start workers after the same failed rewind. All
four isolated tests pass and the DEB candidate was built without installation.
Audio seek clock and preroll work remains open.

## 28 October 2026 — audit fix: detect rejected audio packets, Matinee luxe24

Matinee now reports playback failure when the audio decoder rejects a packet
instead of silently dropping it. All four isolated tests pass with a private
null audio sink; the DEB candidate was built without installation.

## 28 October 2026 — audit fix: protect concurrent Openbox edits, LuxeAppearance luxe31

Appearance now checks the Openbox file against its Apply snapshot and uses a
matching version identifier. A detected conflict preserves the external edit
through rollback. All four isolated tests pass and the DEB candidate was built
without installation. XML parsing and bounded reads remain open.

## 28 October 2026 — audit fix: preserve failed appearance recovery, LuxeAppearance luxe30

Appearance now reports failed rollback removals, keeps the recovery journal
when restoration is incomplete, and allows recovery to be retried. All four
isolated tests pass; the DEB candidate was built without installation. The
focused fixture also covers an end-to-end Apply rollback failure, multiple
failed removals, an obstructed replacement and successful retries.

## 28 October 2026 — audit fix: select a permitted account, LuxePolkit luxe20

The authentication dialog now offers the identities supplied by Polkit when
more than one is permitted. Switching clears the input and starts a fresh
session. Four isolated tests pass and the DEB candidate was built without
installation. Alternate-account success is covered by a mock, and the
private-X fixture verifies pointer and keyboard selection while both input
grabs are held.

## 28 October 2026 — audit fix: unknown Snap refresh policy, LuxUp luxe19

LuxUp now reports an unknown Snap refresh policy when the separate policy
query fails or returns unrecognized output, and distinguishes an indefinite
hold from a finite hold. All three isolated tests pass; the DEB candidate was
built without installation. Per-channel update status remains open.

## 28 October 2026 — audit fix: retain partial update results, LuxUp luxe18

LuxUp now shows updates found in successful checks even when another check
fails, labels the results incomplete and keeps Review available. All three
isolated tests pass; the DEB candidate was built without installation.
Per-channel status remains open.

## 28 October 2026 — audit fix: bound update-command output, LuxUp luxe17

LuxUp now reads command output with separate stdout and stderr limits and
reports overflow, invalid text and timeout errors. All three isolated tests
pass, including concurrent output and cancellation; the behavior test also
passes ASan+UBSan with leak detection disabled. The DEB candidate was built
without installation. Exact Flatpak handoff remains open.

## 28 October 2026 — audit fix: Flatpak installation identity, LuxUp luxe16

User and system Flatpak updates are now kept distinct, with scope and reference
carried through the update service. All three isolated tests pass, and the
behavior fixture passes ASan+UBSan with leak detection disabled. The DEB
candidate was built without installation; bounded command output and exact
store handoff remain open.

## 28 October 2026 — audit fix: APT list freshness, LuxUp luxe15

LuxUp no longer labels a cached, unverified APT check as up to date. It tracks
successful refreshes during the service lifetime and reports stale or unknown
list age, and it flags unrecognized APT output. All three isolated tests pass;
the DEB candidate was built without installation. Per-channel reporting and
freshness across service restarts remain open.

## 28 October 2026 — audit fix: unified terminal preferences, LuxeTerm luxe18

Terminal preference ranges and preset choices now come from the settings
schema and color catalog, and the shipped default file no longer duplicates
schema values. The scrollback control explains unlimited-history storage. Both
terminal tests pass; the behavior fixture also passes ASan+UBSan with leak
detection disabled. The DEB candidate was built without installation.

## 28 October 2026 — audit fix: terminal launch environments, LuxeTerm luxe17

Forwarded terminal launches now receive the invoking environment and PATH;
new tabs and windows inherit their window's launch environment. Both isolated
terminal tests pass, including distinct virtual-environment markers and PATH
lookup; the behavior fixture also passes ASan+UBSan with leak detection disabled.
The DEB candidate was built without installation.

## 28 October 2026 — audit fix: scoped terminal theme updates, LuxeTerm luxe16

Desktop theme changes now calculate one palette and update terminal colors
without reapplying scrollback, fonts, layout or shortcuts. Preference changes
share one palette and rebuild application accelerators once. Both terminal
tests pass; the behavior fixture also passes ASan+UBSan with leak detection
disabled and checks mapped glyph pixels and terminal geometry. The DEB
candidate was built without installation.

## 28 October 2026 — audit fix: one preferences editor, LuxeTerm luxe15

Terminal windows now share one preferences dialog, preventing two stale
settings copies from overwriting each other. Both terminal tests pass, and the
behavior test passes ASan+UBSan with leak detection disabled. The DEB candidate
was built without installation; theme-update efficiency remains open.

## 28 October 2026 — audit fix: protect terminal closes, LuxeTerm luxe14

Manual tab and window closes now ask for confirmation when the preference is
enabled, including a single tab; child exits close without a prompt. The
conservative policy also prompts for idle shells. Both isolated terminal tests
pass, and the behavior test passes ASan+UBSan with leak detection disabled. The
DEB candidate was built without installation.

## 28 October 2026 — audit fix: process and memory read failures, LuxeTask luxe12

LuxeTask keeps its previous process rows and reports an unavailable sample if
enumeration fails, shows unknown CPU or memory when readings fail, and uses the
kernel's available-memory estimate when requested. It rejects malformed or
overflowing memory values. Both LuxeTask tests pass; the DEB candidate was
built without installation. Further metric-validity work remains open.

## 28 October 2026 — audit fix: protect priority targets, LuxeTask luxe11

Priority changes now reject the same protected processes as signals, recheck
the selected process identity and report the OS error on failure. Both LuxeTask
tests pass using only the fixture and its own child; the DEB candidate was built
without installation. The kernel's numeric-PID race remains documented.

## 28 October 2026 — audit fix: require input grabs, LuxePolkit luxe19

Authentication entry remains disabled until the X11 keyboard and pointer are
both grabbed. The dialog retries temporary contention and fails closed with a
specific broker error if it persists. All four isolated agent tests pass,
including contention, recovery and failed-grab cases. The DEB candidate was
built without installation; real-client error presentation remains open.

## 28 October 2026 — audit fix: authentication startup deadline, LuxePolkit luxe18

The agent now terminates a dialog that fails to become ready within 15 seconds.
Once the prompt is ready, users have no added password-entry deadline. All four
isolated agent tests pass, including a stalled child followed by a successful
request and a ready prompt held beyond the test deadline. The DEB candidate was
rebuilt without installation.

## 28 October 2026 — audit fix: bound authentication requests, LuxePolkit luxe18

The authentication broker checks request fields before serialization, and
debug logs omit detail values and prompt text. All four isolated agent tests
pass, including oversized-request recovery and a debug-log fixture. The DEB
candidate was built without installation; the child startup deadline remains
open.

## 28 October 2026 — audit fix: Snap hold recovery, LuxUp luxe14

LuxUp records recovery intent before changing Snap refresh policy, serializes
policy commands and preserves existing finite holds. Failed policy operations
now produce package-hook diagnostics. All three LuxUp tests pass, including
isolated concurrent and partial-failure cases; the DEB candidate was built
without installation. Ownership of an indistinguishable later forever hold
remains an open issue.

## 28 October 2026 — audit fix: original image dimensions, Frame luxe31

Frame now reports file dimensions instead of preview dimensions, even after a
view rotation. Scaled-image zoom is labeled relative to the preview. PNG and
WebP viewer tests cover dimensions, orientation and successive loads. All eight
Frame tests pass, and the viewer and decoder tests pass ASan+UBSan with leak
detection disabled. The DEB candidate was built without installing it.

## 28 October 2026 — audit fix: shared image limits, Frame luxe30

The viewer and wallpaper now share input-size and source-dimension limits,
preview scaling and orientation handling. WebP uses the same bounds and checks
cancellation around decoding. All eight Frame tests pass; focused limit tests
also pass under ASan+UBSan with leak detection disabled. Further hostile-format
profiling remains open.

## 28 October 2026 — fix: rotate wallpapers within the active theme, Frame luxe29

Selecting a bundled wallpaper now rotates among the active theme's installed
orbit, Horizon, River and Veil images daily or on the next login. Custom images
and folders keep their existing behavior. All seven Frame tests pass; an
isolated daemon restart also advanced between two Dark wallpapers. The DEB
candidate was built without installing it.

## 28 October 2026 — audit fix: clipboard save isolation, luxe46

LuxeSession isolates each clipboard save in its own temporary X window so late
replies cannot contaminate a later save. Empty target lists fail promptly. Both
clipboard tests pass ASan+UBSan with leak detection disabled; all 21 session
tests pass and the DEB candidate was rebuilt. Further stress testing remains
open under SES-02.

## 27 October 2026 — audit fix: clipboard transfer limits, luxe46

LuxeSession bounds total save time and outstanding outgoing clipboard transfers,
expires stalled readers, and rejects malformed incremental data. Private-X
fixtures exercise limits, recovery, format-32 chunks and a separate malformed
clipboard owner. Both clipboard tests pass ASan+UBSan with leak detection
disabled, and all 21 session tests pass. Further adversarial load and generation
coverage remains open under SES-02.

## 27 October 2026 — audit fix: clipboard save deadlines, luxe46

LuxeSession now times out stalled incoming clipboard targets while allowing
active incremental transfers to make progress. Private-X deadline, recovery and
clipboard persistence tests pass, as do all 21 session tests; the deadline
fixture also passes ASan+UBSan with leak detection disabled. Outgoing transfer
limits and a whole-save deadline remain open under SES-02.

## 27 October 2026 — audit fix: clipboard memory budget, luxe46

LuxeSession deduplicates clipboard targets, limits retained target buffers and
continues after rejecting an oversized incremental transfer. Private-X and
budget tests pass; the budget fixture passes ASan+UBSan with leak detection
disabled. All 21 session tests pass after correcting the separate header-test
baseline; stalled-transfer deadlines remain open under SES-02.

## 27 October 2026 — audit fix: terminal header test baseline

The session's real-terminal test now removes compact styling from both the
disposable baseline theme and its bridge. The original compact-height and button
checks pass, as does the full session suite. This changes test setup only.

## 27 October 2026 — audit fix: battery summary and brightness, luxe12

LuxePower combines multiple battery readings consistently and restores the
observed brightness after a denied change, without losing a newer pending
request. Disposable hardware fixtures and all four normal and sanitizer suites
pass; sanitizer leak detection was disabled. Per-battery threshold controls
remain open under POWER-04.

## 27 October 2026 — audit fix: shortcut reload recovery, luxe13

LuxeHotkey keeps its current shortcuts and pending edits if an Openbox reload
fails. Malformed-file and successful-reload fixtures and all four normal and
ASan+UBSan tests pass; sanitizer leak detection was disabled. Dirty-edit
confirmation remains open under HOT-03.

## 27 October 2026 — audit fix: shortcut validation, luxe13

LuxeHotkey rejects unsupported modifiers and invalid keys before changing
Openbox bindings, and compares modifier aliases and ordering consistently when
checking collisions. CLI and direct plugin tests and all four package suites
pass. Keyboard-layout and existing-binding coverage remains open under HOT-02.

## 27 October 2026 — audit fix: notification monitor changes, version 9

LuxeNotify repositions existing cards after monitor, workarea or workspace
changes and disconnects its watches at shutdown. Private-X repositioning and
all six normal tests pass; the behavior fixture passes ASan+UBSan with leak
detection disabled. Overflow behavior remains open under NOT-03.

## 27 October 2026 — audit fix: notification image retention, version 9

LuxeNotify scales large raw image hints to the display icon size before
retaining the texture. Image validation and aspect-ratio tests and all six
normal suites pass; the behavior fixture passes ASan+UBSan with leak detection
disabled. Peak input memory and file-icon byte limits remain open under NOT-01.

## 27 October 2026 — audit fix: notification icon cancellation, version 9

LuxeNotify cancels pending icon open and decode work when a notification is
closed or replaced, ignores late decoded images, and limits outstanding file
icon loads to eight. Lifecycle and burst fixtures and all six normal tests
pass; the behavior fixture also passes ASan+UBSan with leak detection disabled.
Icon byte limits remain open under NOT-01.

## 27 October 2026 — audit fix: notification text bounds, version 9

LuxeNotify bounds notification text before markup and layout, truncates on
UTF-8 boundaries, and limits visible summary and body lines. Long action keys
are skipped rather than altered. Multibyte fixtures and all six notification
test suites pass. Icon-work limits remain open under NOT-01.

## 27 October 2026 — audit fix: notification renderer timeouts, version 9

LuxeNotify now replaces a renderer after a request times out or its transport
fails, while preserving the public notification service. A private-bus fixture
checks recovery and ordinary method rejection; all six notification suites
pass. Generation and repeated-crash handling remain open under NOT-02.

## 27 October 2026 — audit fix: icon build ownership, version 9

Luxe Icons checks generated-output ownership before replacing or deduplicating
a tree and verifies archive hashes before removing previous output. Fresh and
repeat builds and all three icon tests pass. Older unmarked build directories
are refused; staged replacement remains open under ICO-01.

## 27 October 2026 — audit fix: Matinee shortcuts, luxe23

Matinee's shortcut help and registered accelerators now share one list. Stop
has its listed shortcut, and unavailable volume and mute shortcuts are no
longer listed. About uses the suite website and copyright. Shortcut and
injected media-read failure fixtures pass with the full Matinee suite.

## 27 October 2026 — audit fix: Matinee paused and failed seeks, luxe23

Seeking while paused no longer starts playback. Explicit seek failures are
reported and do not claim the requested position. Video, failure-injection
and window-dialog fixtures pass with the full Matinee suite. Audio clock
anchoring and other seek work remain open under MAT-05.

## 27 October 2026 — audit fix: Matinee chooser lifetime, luxe23

Matinee cancels outstanding file and folder choosers on window disposal and
ignores late results safely. Repeated chooser tests check filter cleanup; all
four normal and sanitizer test suites pass (sanitizer leak detection disabled).

## 27 October 2026 — audit fix: Matinee mixed file requests, luxe23

Matinee now opens every local file in a mixed request and explains skipped
non-local inputs. A remote-only request preserves the current playlist. The
title and artist buffers also belong to their respective player engines, and
spectrum analysis accumulates complete windows from both stereo channels and
skips video playback where its visualization is hidden. FFT scratch space and
window coefficients are reused during playback.
The private-display behavioral fixtures and all four Matinee test suites pass.

## 27 October 2026 — audit fix: icon cache and sidebar ownership

Luxe Icons `2026.09.09-8` generates cache hooks for the configured data
directory; disposable-prefix tests and all three icon tests pass. Lucid
`0.1.0-1+luxe20` releases sidebar fallback file objects after activation;
its ownership fixture and both test suites pass.

## 27 October 2026 — audit fix: Shotty action preference, luxe18

Shotty now stores its screenshot action as one choice and migrates legacy
settings deterministically. Multiple action windows follow the same choice.
The private-display behavioral suite passes with an isolated settings backend.
Synchronous capture remains open under SHOT-03.

## 27 October 2026 — audit fix: bounded TLP configuration reads, luxe11

LuxePower now enforces its 4 MiB TLP configuration limit while reading,
preventing oversized files from being loaded into memory before rejection.
It also rejects embedded NUL bytes that could otherwise cause the remainder of
the configuration to be lost on a later write. Boundary, malformed-file and
missing-file fixtures and all four LuxePower test suites pass.
Probe timeouts and cancellation remain open under POWER-03.

## 27 October 2026 — audit fix: panel clock boundary, luxe53

LuxePanel schedules minute updates at the next minute boundary rather than a
second late. Boundary arithmetic tests pass normally and under ASan+UBSan with
leak detection enabled; all 26 panel tests pass. Clock-change handling and
shared policy work remain open under PAN-07.

## 27 October 2026 — audit fix: icon-theme snapshot opening, luxe29

LuxeAppearance now opens the selected archive without following a final
symlink, verifies the opened file is regular and enforces the compressed-size
limit while copying. Disposable fixtures cover a symlink swap and an oversized
sparse file; all four appearance tests pass. Safe member parsing and bounded
extraction remain open under APP-02.

## 27 October 2026 — audit fix: icon-theme publication, luxe28

LuxeAppearance stages each icon theme on the destination filesystem and
publishes it with an atomic no-replace rename. A competing destination is
preserved and only the installer's staging directory is cleaned up. The
installer fixture covers the conflict and all four appearance tests pass;
archive validation and installer feedback remain open.

## 27 October 2026 — audit fix: icon-theme archive snapshot, luxe27

LuxeAppearance validates and extracts the same private archive snapshot,
limiting compressed input to 128 MiB and the copy to 30 seconds. A disposable
fixture replaces the original archive after validation and verifies the
validated snapshot is installed; all four appearance tests pass. Safe archive
member parsing, extraction limits and atomic publication remain open.

## 27 October 2026 — audit fix: panel notification ordering, luxe52

LuxePanel rejects notification-count replies superseded by a newer signal or
service owner and cancels queries when the service or plugin disappears.
Delayed-callback and private-bus fixtures cover signal/reply ordering and an
owner restart; both pass under ASan+UBSan with leak detection disabled. The
24-test panel suite also passes. Palette policy remains open under PAN-06.

## 27 October 2026 — audit fix: confirmed display saves, LuxeRandR luxe13

LuxeRandR refuses to save edited display settings until they have been applied
and confirmed, preserving the existing autostart file on refusal. A private-X
fixture uses mocked xrandr commands to cover refusal and confirmed Save; both
LuxeRandR tests pass. Exact topology and rollback watchdog work remain open.

## 27 October 2026 — audit fix: panel Run completion, luxe51

Run completion now deduplicates through a hash table, skips executable
directories, handles empty PATH components, and runs at most one scan worker
with one latest pending dialog. Path and delayed-worker fixtures pass under
ASan+UBSan with leak detection disabled; all 23 panel tests pass. A blocked scan
can still delay completion and result sizes are unbounded (PAN-05).

## 27 October 2026 — audit fix: panel directory paths, luxe50

The directory menu retains raw filesystem paths separately from display labels
and sorts its collected entries once. A private-display fixture verifies
navigation into a folder with invalid UTF-8 bytes and passes ASan+UBSan with
leak detection disabled. Synchronous, unbounded enumeration remains open
under PAN-04.

## 27 October 2026 — audit fix: panel tray-transfer expiry, luxe49

LuxePanel discards incomplete tray messages after a 60-second deadline when
subsequent tray traffic arrives, freeing capacity without an idle timer. The
synthetic-event fixture passes with ASan+UBSan and leak detection; all 20 panel
tests pass. Real XEmbed flood and timeout coverage remains open under PAN-03.

## 27 October 2026 — audit fix: panel tray-message limits, luxe48

LuxePanel bounds outstanding tray balloons globally and per client, gives
invalid or zero display timeouts a finite default, and cancels only the
requested incomplete message ID. A synthetic-event fixture exercises both
queue limits and passes ASan+UBSan with leak detection enabled. Incomplete
transfer expiry and real XEmbed flood coverage remain open under PAN-03.

## 27 October 2026 — audit fix: Matinee A/V completion, luxe22

Matinee waits for both audio and video to finish and paces a longer video tail
with a pauseable clock after audio drains. Stop/seek release the resampler so
replay does not retain old samples or an old output rate. Private null-device
fixtures cover both stream-completion orders and pause/resume; all four tests
pass normally and under ASan+UBSan with leak detection disabled. Real-device,
delayed-frame and seek-continuity checks remain open under MAT-03/MAT-04.

## 27 October 2026 — audit fix: Matinee audio-end drain, luxe21

Matinee drains decoded audio, resampler output and the ALSA device before
signaling audio completion. A null-device fixture verifies the resampled tail,
holds device drain to check completion ordering, and checks Stop during drain.
All four tests pass normally and under ASan+UBSan with leak detection disabled.
Longer video tails and real-device drain behavior remain open under MAT-04.

## 27 October 2026 — audit fix: Matinee audio failures, luxe20

Matinee now posts an error to the UI when its audio worker encounters an
unrecoverable read, decode, allocation, conversion or output failure, and
allows a later playback retry. Null-device tests cover a failed write, retry,
main-thread notification and Stop canceling a queued notification. All four
Matinee tests pass normally and under ASan+UBSan with leak detection disabled.
Detailed error messages and correct end-of-stream draining remain open.

## 27 October 2026 — audit fix: Matinee partial audio writes, luxe19

Matinee retries unwritten audio frames and handles interrupted, temporarily
unavailable and recoverable underrun results. A null-device fixture checks
sample continuity and cancellation after partial progress; all four Matinee
tests pass, as does the suite under ASan+UBSan with leak detection disabled.
Playback error reporting and draining remain open under MAT-03 and MAT-04.

## 27 October 2026 — audit fix: Matinee upsampling capacity, luxe18

Matinee sizes each resampler output buffer for the converter's estimated output
rather than the input frame count, with a bounded fallible allocation. A null
audio-device test covers 8 kHz to 48 kHz conversion; all four Matinee tests
pass. Short writes, error reporting and playback draining remain open under
MAT-03 and MAT-04.

## 27 October 2026 — audit fix: Matinee negotiated audio rate, luxe17

Matinee now uses the rate negotiated with ALSA for both resampling and its
audio clock. A private null-device fixture exercises a 32 kHz output rate; all
four Matinee tests pass. Resampler output sizing, short writes and playback
draining remain open under MAT-03 and MAT-04.

## 27 October 2026 — audit fix: panel launcher-drop teardown, luxe47

LuxePanel cancels pending launcher drops before plugin teardown and rejects
oversized decoded payloads before copying or parsing. A focused delayed-callback
fixture passes ASan+UBSan with leak detection enabled. Real transfer lifetime
coverage and a bounded streaming reader remain open under PAN-02.

## 27 October 2026 — audit fix: panel task-icon bounds, luxe46

LuxePanel validates task-icon X11 property types, formats, lengths and image
dimensions before reading them, and bounds legacy pixmap/mask conversion.
Private-X tests cover malformed and valid properties; all 18 panel tests pass,
and the focused fixture passes ASan+UBSan with leak detection disabled.
End-to-end malformed-icon rendering and X fault coverage remain under PAN-01.

## 27 October 2026 — audit fix: Lucid modeline bounds, luxe19

Lucid skips modeline candidate lines longer than 8,192 characters instead of
copying the entire line. Private-display tests cover oversized first and last
lines and ordinary Vim modelines; both Lucid tests pass. Additional preference
routing and large-document performance work remain open under LUC-04.

## 27 October 2026 — audit fix: Lucid hidden sidebar, luxe18

Lucid no longer enumerates or monitors a sidebar folder while the sidebar is
hidden; showing it reloads the retained folder. Sidebar and window-geometry
settings no longer reapply every document's preferences. Private-display tests
cover hiding, showing and retained folder state; both Lucid tests pass. Other
preference and modeline work remains open under LUC-04.

## 27 October 2026 — audit fix: Lucid recovery scheduling, luxe17

Lucid replaces the recurring recovery poll with one-shot timers for edits,
metadata and retries. Settled windows have no recovery timer; repeated cursor
changes share a longer metadata deadline. Private-display tests cover timer
coalescing, automatic completion and retry after a busy document. Both Lucid
tests pass. Metadata-only snapshots still serialize retained dirty text and
LUC-03 remains open.

## 27 October 2026 — audit fix: Lucid recovery warnings, luxe16

Lucid now warns when recovery omits tabs or unsaved content, including the
100-tab limit. The inline warning remains until a complete current recovery
snapshot is saved successfully. Private-display tests cover oversized content,
more than 100 tabs, a failed write and successful retry; both Lucid tests pass.
Detailed omission reporting and preservation remain open under LUC-02.

## 27 October 2026 — audit fix: asynchronous Shotty save, luxe17

Shotty now encodes and writes local screenshot saves on a cancellable worker,
keeping the interface responsive and closing only the completed screenshot
window. A private-display regression holds a write, checks a GTK heartbeat,
closes the window and verifies the original file and staging cleanup. The
behavior suite passes normally and with ASan+UBSan (leak detection disabled).
Metadata, durability and competing-writer work remain open under SHOT-01.

## 27 October 2026 — audit fix: Shotty staged save, luxe16

Shotty writes local PNG saves to an exclusive temporary file before replacing
the destination, preserving the previous file if encoding or close fails.
Tests cover an injected write failure, a detected competing change, symlink
rejection, permission bits and successful decoding. The behavior suite passes.
Save still blocks the interface, and metadata, durability and remaining
concurrent-writer races are tracked under SHOT-01.

## 27 October 2026 — audit fix: Frame wallpaper scan reuse, luxe28

Frame reuses the wallpaper application's candidate count when scheduling daily
rotation, avoiding an immediate second directory scan. A private-display test
counts directory reads and verifies rotation is still scheduled; all seven
Frame tests pass. Synchronous preparation remains open under FRA-04.

## 27 October 2026 — audit fix: Frame wallpaper save errors, luxe27

Frame reports a wallpaper as displayed but unsaved when settings persistence
fails, and returns a failure status to the command caller. A private display
and bus test exercises a real service and failed settings write; all seven
Frame tests pass. Transactional persistence and chooser feedback remain open
under FRA-05.

## 27 October 2026 — audit fix: Frame wallpaper arguments, luxe26

Frame now rejects wallpaper commands missing a path before contacting the
wallpaper service, with a diagnostic and failure status. A headless fixture
checks both commands; all six Frame tests pass. Persistence and folder-chooser
failure reporting remain open under FRA-05.

## 27 October 2026 — audit fix: Frame wallpaper scan bound, luxe25

Frame counts all wallpaper-directory entries toward its 4,096-entry scan limit
and logs when the directory is truncated. The README explains that the subset
comes from filesystem order. A temporary directory with 4,100 non-image files
verifies the bound. Wallpaper preparation remains synchronous and FRA-04 stays
open.

## 27 October 2026 — audit fix: session terminal launch status, luxe45

The default-terminal helper reports and returns a failed D-Bus request instead
of claiming success. A disposable D-Bus command stub verifies success, failure
and a working directory containing spaces. Nineteen session tests passed with
the previously documented real-header baseline test excluded. Remaining
session identity and configuration consistency work is tracked as SES-06.

## 27 October 2026 — audit fix: Shotty cache checks, luxe15

Shotty reports cache creation failures and refuses to place screenshot
handoffs in a final directory that is a symlink, is owned by another user, or
allows group or other access. Temporary-directory tests cover private,
permissive, symlink and non-directory cases. Race-resistant directory access,
retention and crash cleanup remain open under SHOT-02.

## 27 October 2026 — audit fix: Shotty handoff activation, luxe14

A pending viewer handoff no longer prevents another screenshot from starting.
Completing one screenshot closes its own window without terminating other
captures or handoffs. Private-display tests cover reactivation, concurrent
clipboard completion and handoff cleanup, and pass with ASan+UBSan (leak
detection disabled). The fixed handoff timeout and cache cleanup remain open
as SHOT-02 in `areport.md`.

## 27 October 2026 — audit fix: Shotty handoff descriptor, luxe13

Shotty now encodes viewer handoff images through the exclusively created file
descriptor, reporting write and close failures rather than reopening its path.
A test verifies the original descriptor remains the target after a path change
and that a full filesystem reports an error. The Shotty behavior suite passes;
ordinary screenshot Save remains synchronous and non-transactional (SHOT-01).

## 27 October 2026 — audit fix: PCManager preference persistence, luxe37

PCManager saves only the preference changed by a window, preventing a stale
window from rewriting unrelated settings during normal sequential use. Failed
writes and unsafe preference directories now produce an error in the window and
a diagnostic. The private-display test covers preservation of an unrelated
setting and a real replacement failure. Concurrent writers still need a
transaction and PCM-05 remains open.

## 27 October 2026 — audit fix: PCManager Trash paste confirmation, luxe36

Pasting files into Trash now asks before moving the originals and explains that
copied files are also moved from their current locations. Cancel is the default;
canceling preserves the files and clipboard. A private-display regression covers
the real clipboard, warning and cancellation path without touching the real
Trash. External cut interoperability and partial-move handling remain open as
PCM-04 in `areport.md`.

## 27 October 2026 — audit fix: PCManager monitor refreshes, luxe35

PCManager `1.4.0+gtk4-4+luxe35+desktop-cairo3` coalesces directory-monitor
bursts with a quiet period and bounded scheduling deadline, defers hidden-tab
refreshes, and retains a pending refresh while a model is loading. Private-X
tests reproduce the earlier burst rebuild and verify selection, hidden tabs,
in-flight loads, a GTK heartbeat under thousands of synthetic notifications,
and real temporary-file creation/rename. All ten package tests pass; the new
test also passes three consecutive runs and ASan+UBSan with leak detection
disabled. PCM-03 remains partial: full model rebuilds, disk-space queries,
remote/slow backends and high-rate real filesystem workloads need more work.
The candidate DEB was built and inspected but not installed.

## 27 October 2026 — audit fix: PCManager directory copy staging, luxe34

PCManager `1.4.0+gtk4-4+luxe34+desktop-cairo3` rejects known destination
conflicts before copying directory contents, creates one top-level staging tree
for nested copies, and creates local staging directories privately from the
start. The final no-overwrite move still protects a destination created during
the copy. Local tests reproduce the old extra work and exposed staging mode and
cover races, symlinks, nested failure, cancellation and permissions; the
focused suite also passes ASan+UBSan with leak detection enabled. PCM-02 remains
partial: remote staging privacy, cleanup errors/deadlines and metadata-copy
failure handling remain open. All nine package tests pass. The candidate DEB was built and inspected but not
installed; the interim luxe33 candidate was superseded.

## 27 October 2026 — audit fix: ObConf desktop names, luxe14

LuxeAppearance ObConf `2:0.2.3+gtk4-4+luxe14` consistently limits desktop
names to 255 Unicode characters, validates before saving, and keeps Apply dirty
when an invalid name or X11 update prevents completion. Empty names survive
reopening. Private-X tests cover ASCII and multibyte limits, file and X11 state,
invalid input and reopening; all three package tests pass, as does the lifecycle
test under ASan+UBSan with leak detection disabled. A live X11 failure after a
successful XML save can still leave the file updated while Apply remains dirty;
that failure was not injected. The candidate DEB was built and inspected but
not installed.

## 27 October 2026 — audit fix: Shotty capture bounds, luxe12

Shotty `0.1.0-1+luxe12` limits capture rectangles to 32 Mi pixels before
image allocation, reports capture errors in the menu, and leaves region
cancellation quiet. Invalid and non-finite selection coordinates are rejected
before integer conversion. Private-X tests verify the limit without allocating
a large image, error/cancellation handling and existing capture behavior; the
suite also passes ASan+UBSan with leak detection disabled. This partially
addresses SHOT-03; capture remains synchronous, action-state cleanup and
whole-process memory profiling remain open. The candidate DEB was built and
inspected but not installed.

## 27 October 2026 — audit fix: ObConf theme ownership, luxe13

LuxeAppearance ObConf `2:0.2.3+gtk4-4+luxe13` releases selected theme names,
avoids leaking temporary XML strings, and ignores asynchronous dialog or
installer results belonging to an older editor window. A headless XML fixture
reproduced 58,000 bytes of leaks and now passes LeakSanitizer; a private-X test
reproduced stale installation results changing the new editor and now passes,
including a real disposable archive installation across close/reopen. All three
package tests pass. OBC-05 remains partial pending real file-dialog lifecycle,
repeated selection and broader leak verification; a whole-GTK leak run reported
fontconfig and untraced GLib allocations. The luxe13 DEB was built and inspected
but not installed; the interim luxe12 candidate was superseded.

## 27 October 2026 — audit fixes: PCManager IPC, luxe32

PCManager `1.4.0+gtk4-4+luxe32+desktop-cairo3` reads single-instance requests
without blocking the main loop, retains fragmented lines, rejects incomplete
requests and expires idle clients. It bounds total retained request bytes across
clients, uses close-on-exec descriptors, and cleans up correctly if a request
callback shuts down the server. A real-socket fixture reproduced the original
freeze and passes fragmented-input, shared-budget, 32-client timeout, cleanup
and reentrant-finalization cases, including an ASan+UBSan run. Outgoing
connections and sends are bounded, reject oversized requests and do not raise
SIGPIPE if the peer closes. It also rejects
overlong socket paths before they can be truncated and used to unlink another
file (PCM-06), and correctly derives X display numbers with screen suffixes
(PCM-07). All nine package tests passed before the final connection/display
changes; the four final IPC checks pass afterward. PCM-01 remains partial:
EOF-only framing can still accept a timed-out request prefix and there is no
acknowledgement. The audit's original claim that malformed options necessarily
leave global values changed was not reproduced for the tested GLib option
types. The luxe32 candidate DEB was built and inspected but not installed;
interim luxe30 and luxe31 candidates were superseded.

## 27 October 2026 — audit fix: Slab launcher refresh, testing2

Experimental Slab `0.2.9~testing2` refreshes a retained button's desktop-entry
executable and icon after metadata changes while preserving its widget and
focus. Hidden entries fall back to running-window identity; NoDisplay remains
usable for running or intentionally pinned applications. A private-X test
reproduced the old executable, verifies the updated harmless fixture launch,
icon and focus, and covers hidden entries, removal and reinstallation. All five
Slab tests pass; the monitor callback was invoked directly in the regression.
The testing2 DEB was built and inspected but not installed or promoted, and the
package remains excluded from public release and CI.

## 27 October 2026 — audit fix: Appearance Openbox writes, luxe26

LuxeAppearance `0.3.0+gtk4-2+luxe26` returns failure when an Openbox theme
rewrite cannot match the configuration, instead of reporting the earlier read
as a successful write. It also creates a missing user configuration when the
selected theme already matches the template. Regressions reproduced both
issues and verify the rejected file is unchanged and the template is created;
all four package tests pass. This
only partially addresses APP-04: XML-aware matching and coherent bounded
read/ETag handling remain open. The candidate DEB was built and inspected but
not installed; an interim luxe25 candidate was superseded.

## 27 October 2026 — audit fixes: LuxuryStore source parsing, 0.1.0-28

LuxuryStore uses one conservative repository parser for the UI, catalogue,
privileged helper and rewrite. It can re-enable its own disabled scoped list
entries while preserving unrelated commented sources; every actionable source
must have an absolute Signed-By path and cannot request authentication bypass.
Unsupported, ambiguous and mixed-state definitions are not rewritten. APT
remains responsible for validating key contents and repository authentication.
Pure regression coverage and all nine private-X CTests pass. Candidate DEB
0.1.0-28 was built and inspected; intermediates 0.1.0-26 and 0.1.0-27 were
superseded, and none was installed. Concurrent administrator edits and GUI rendering of all
error states remain untested; see areport.md.

## 27 October 2026 — audit fix: Openbox title layout, luxe11

LuxeAppearance ObConf `2:0.2.3+gtk4-4+luxe11` sanitizes title-layout input
in a single pass, removing invalid characters and repeated buttons without
skipping the next character. The callback and XML test reproduced the old
invalid-byte failure and now verifies mixed case, punctuation, duplicates,
empty input and UTF-8. The tree test's pre-existing invalid unref was traced to
its own window-lifetime fixture and corrected with a weak-reference check
(OBC-06). Both package tests pass with GLib criticals fatal. The candidate DEB
was built and inspected but not installed.

## 27 October 2026 — audit fix: Frame desktop URI, luxe24

Frame `0.1.0-1+luxe24` explicitly passes one URI in its desktop entry. Tests
verify argument expansion for a local filename containing spaces and non-ASCII
text, no-file launch, image loading through Frame's application-open path and
window reuse. These launcher and viewer tests run separately; other desktop
launchers and remote URIs were not tested. GIO already supplied the file with
the old entry, so the original audit claim was too broad and is corrected in
areport.md. All five Frame tests and desktop-file validation pass. A candidate
DEB was built and inspected but not installed.

## 27 October 2026 — audit fix: LuxuryStore repository Enabled value, 0.1.0-25

LuxuryStore now replaces the old deb822 `Enabled:` value instead of appending
to it, preserving indentation and CRLF on that field. A focused pure C++ test
reproduced the previous invalid value and covers enable/disable, repeated enable,
CRLF and duplicate fields. All nine CTests pass under private X; the initial GUI
run was interrupted after it inherited the desktop display. The candidate DEB
was built and inspected but not installed. This is a partial STORE-01 fix: the
shared paragraph parser and unsupported-state handling remain open, as do
STORE-02 signing-key and trust-validation issues; see areport.md.

## 27 October 2026 — audit fix: preserve session autostart migration, luxe44

LuxeSession `0.5.5+gtk4-4+luxe44` fixes SES-03: failed legacy autostart
conversions keep their source for retry, report failure without exposing command
arguments, and retain successful conversions. Equivalent entries are not rewritten;
successful retirement preserves existing backups and creates a private, exclusive
backup before removing the source. Concurrent writes cannot be fully excluded in
the final source-check/unlink window.

The new temporary-tree regression reproduced the old loss and passes for partial
failure, retry, preexisting backups, malformed commands and unsafe FIFO sources.
Eighteen session tests pass with the previously documented real-header failure
excluded. ENOSPC, permission-denial and concurrent-writer injection remain untested.
The candidate DEB was built and inspected but not installed; RPM generation needs
rpmbuild. The required session migration runtime continues to use Python.

## 24 October 2026 — audit fix: Lucid recovery preservation, luxe15

Lucid `0.1.0-1+luxe15` addresses areport.md LUC-01. Opening a file now runs
the same recovery initialization as ordinary activation. Previous unsaved edits
are durably preserved in Sessions before new snapshots may replace the primary
recovery. Restore-off retains those edits and explains how to recover them.
Malformed recovery and preservation conflicts keep the original untouched and
use a separate destination for current recovery. Identical preserved states
are deduplicated; saved recovery copies are retained until removed.

Three state cases and seven editor cases pass, including six new startup
scenarios. The original file-open regression failed before the fix. Tests
cover preferences, malformed/conflicting data, clean-file deduplication,
private permissions and final symlinks under disposable state/private X.
The state helper/test now uses GIO without a GTK dependency. The final luxe15
DEB builds and passes payload inspection; the interim local luxe14 candidate
was not promoted. No package was installed. Full-disk injection and the
separate recovery-limit/scheduling findings remain open in areport.md.

## 24 October 2026 — audit fixes: Matinee controls/ownership and Appearance chooser

Matinee `0.1.0-1+luxe16` fixes MAT-01, MAT-02, MAT-06 and MAT-12:
library scans release GFile correctly, volume affects the PCM actually written
to ALSA, playlist edits preserve playback identity with correct GTK notifications,
rows activate and follow Previous/Next, and production main initializes Cairo.
Paused/ended/error playback also stops animation and position timers while
retaining its last frame (partial MAT-09; spectrum/FFT work remains).

Appearance `0.3.0+gtk4-2+luxe24` fixes APP-01's chooser double-unref. Binary,
test and package version labels consume the package's existing version source;
Matinee release labels likewise derive from one revision variable.

New regressions reproduced each ownership/control/initialization defect before
the fix. Matinee's four CTests pass, including 16 behavior checks and three
production-entry/environment cases. Appearance's four CTests pass. Targeted
ASan/UBSan ownership tests pass with leak detection disabled; weak references
verify the relevant finalization. Audio tests use a private ALSA null device.
Both DEBs build and pass payload/version and candidate ownership checks. RPM
attempts stop at the missing rpmbuild tool. No candidate was installed or
promoted. areport.md records the exact fixes, partial findings and remaining
media drain/seek/cancellation work.

## 24 October 2026 — audit fixes: confirmed locking before sleep

LuxeSession `0.5.5+gtk4-4+luxe43` and LuxeDM `1.4.1-1+luxe16` address
areport.md SES-01. Suspend and Hibernate now wait for the native locker to
confirm its cover, input grabs, PAM setup and panel are ready. Failed startup,
timeout or cancellation keeps sleep blocked and reports an error. Closing the
dialog prevents a late sleep request and never terminates an active lock.
Already-ready native locks can acknowledge another request. Manual external
locker fallbacks remain; actions requiring confirmed readiness need the native
backend. The shared client adds no resident service.

LuxePower `0.2.0-10` uses that same async client for both manual Lock actions,
prevents duplicate submissions and handles window closure. Its idle helper now
checks the lock child's exit result. These address the lock portion of POWER-04;
brightness and battery findings remain open.

Session's 18 tests excluding the known real-header baseline failure pass.
New tests cover delayed/failed locking, invalid acknowledgement, timeout,
cancellation, the actual shell wrapper, mocked sleep gating and closed windows.
All four LuxePower tests pass. Native private-X tests cover readiness lifetime
and a disconnected requester. All three candidate DEBs build and pass payload
checks; their combined ownership check passes. RPM building requires rpmbuild,
which is unavailable. Candidates were not installed or promoted. The report
records remaining production PAM/RandR acceptance and every test's scope.

## 24 October 2026 — audit fixes: LuxeDM lock and account safety, luxe15

LuxeDM `1.4.1-1+luxe15` addresses areport.md DM-01, DM-02 and DM-03.
The locker keeps its live X connection, opaque cover and input grabs when
TERM/INT/HUP/QUIT arrive instead of re-executing. A separate X geometry watcher
resizes the cover while PAM waits for input; newly exposed space is black.
Automatic login now requires PAM_SUCCESS from account management, rejecting
password-change requirements, errors and unknown results.

A fresh Release build and all 14 CTests pass. New tests exercise retained
keyboard/pointer grabs under repeated signals, geometry changes with a blocked
UI thread, and 66 mocked PAM results. The PAM regression failed on the old
implementation. DEB built and passed payload audit; candidate dependency and
RPATH checks passed. RPM generation cannot complete because rpmbuild is absent.
No package was installed or promoted and the live desktop was not locked or
restarted. Physical RandR/PAM acceptance and the separate lock-before-sleep
readiness fix (SES-01) remain open. Full details and remaining work are tracked
in areport.md. The 24-package source hygiene check passes after moving the
earlier audit's generated DebInstall interpreter cache out of the source tree.

## 24 October 2026 — Python tooling conversion completed

Maintained build, packaging, diagnostic and non-DebInstall test tooling now uses
C++17, CMake and shell. The final conversions cover panel workload/initialization
profiling, six-palette visual/tray checks, MIME defaults, compositor behavior,
session-migration regression fixtures and GTK3/GTK4/libadwaita widget probes.
Inline archive-test generators now use GNU tar. DebInstall application code and
its Python tests, plus the packaged session migration program, are retained.

Package builds prepare native helpers before timed CTests. The nested runner
retains its 90-second default and accepts an explicit bounded timeout for long
diagnostics. A clean native-tools build and three regressions pass; all five
fresh LuxeNotify tests pass. A fresh LuxeSession build succeeds with Python
blocked in PATH, and 15 of its 16 tests pass. The real-header test also fails in
the original harness because both baseline and compact headers are 32 pixels;
its assertion remains. Separate tall-baseline fixtures exercise its full
pointer/input checks. Panel profiling completed 28 workload records and 72
initialization records. No installed packages or release artifacts were changed.

## 24 October 2026 — native application checks and theme importer

Converted notification, authentication boundary, icon coverage/lookup/ownership,
wallpaper collection, terminal forwarding, session editor, Frame wallpaper and
LuxUp service fixtures to C++ or shell. Panel taskbar, restart and Run-dialog
fixtures also use C++. Tests retain private displays/configuration and their
behavioral assertions. Lucid’s maintained palette importer now uses C++ and
produces equivalent XML. DebInstall and Python application runtimes are unchanged.

## 24 October 2026 — native diagnostics and additional test fixtures

Twelve shared memory/XRes/image diagnostic entry points now use C++17, retaining
private profiles, process sampling, JSON reports and cleanup. Shell wrappers
replace the old `.py` commands; historical records retain their original command
names. All modes were exercised against private test displays or disposable
fixtures, without changes to the installed desktop or release artifacts.
LuxuryStore's local APT fixture, menu-layout checks, default-panel checks and
LuxeDM restart-policy checks also use shell or CMake. Their CMake callers were
updated; application runtime Python remains untouched.

## 24 October 2026 — native build and packaging tools

Replaced Python source hygiene, isolated X test orchestration, DEB payload and
ownership audits, artifact organization and downloaded-cohort collection with
C++17, shell and CMake. Icon deduplication now uses C++17 with byte-for-byte
comparison. Frozen rollback selection, Debian version ordering, checksums and
payload checks remain. Removed eight obsolete completed GTK/resident migration
scripts with user approval. DebInstall and session migration runtime are unchanged.
Application Python fixtures and diagnostics remain; this is not a claim of a
Python-free test suite. No package was installed or promoted.

Validation includes seven shared GTK tests under the native runner, positive
and negative package fixtures, source checks across 24 packages, ownership
audit of 25 existing DEBs, and complete 24-package collection into a disposable
build directory. The current and rollback artifact cohorts were not changed.

## 23 October 2026 — Lucid header bar restored, luxe13

Lucid's window had no header bar: no New/Open/Save buttons, find or terminal
toggle, and no main menu, so Preferences and About were unreachable and the
editor looked like a bare text box. `setup_window()` handed the toolbar view to
`luxe_window_set_content()` before the header bar was added to it, so the shared
helper found no titlebar to attach. The call now follows
`luxe_toolbar_add_top_bar()`, matching every other Luxe application. The
editor integration test now asserts the titlebar, its main menu button and the
Preferences and About actions (it failed before the fix). Version advanced to
`0.1.0-1+luxe13`. DEB built with CPack, payload audit passed, and promoted to
`artifacts/debs/` (luxe9 remains the frozen rollback); both checksum lists
verify. Not installed here; RPM not built (no rpmbuild).

## 23 October 2026 — documentation consistency pass

Documentation only; no source, version or package change. The Bible's package
table now lists LuxePanel `luxe45` (matching `luxepanel/CMakeLists.txt`, the
current DEB and `manifest.json`) and records the experimental `slab` testing
build. Build examples use `$LUXE_ROOT` instead of a hardcoded home path.
`DISTRO.md` now counts 24 packages and gives the current DEB footprint
(26,127,590 bytes, excluding slab). `artifacts/README.md` and
`artifacts/UNRELEASED.md` note that `debs/` also holds the slab testing DEB.
`cruft.md` is marked resolved; its CRUFT-06 header cleanup was verified
complete. The README gains a short current-status section.

Follow-up the same day: `LUXE-BIBLE.md` was restructured. Its per-component
behavior and memory-pass paragraphs (LuxeDM, Frame, LuxePanel, PCManager,
LuxuryStore/LuxUp, Matinee, Polkit/Notify, Shotty, browser defaults, slab and
the 9 October RAM notes) moved verbatim to the new `LUXE-COMPONENTS.md`; the
Bible keeps rules, the package table, build instructions and conventions, plus
short component summaries. `DISTRO.md` was reordered (layout presets and the
next step moved) and notes that slab is not a Plank fork. `hreport.md`,
`astra.md`, `light.md`, `shortage.md` and `changelog.md` gained orientation
banners (`hreport.md` is marked a historical snapshot whose libadwaita advice is
superseded). `artifacts/README.md` gained a report index, `slab/README.md` lost
hardcoded home paths, and `luxenotify/README.md` documents the broker/child
design.

## 13 October 2026 — LuxeDM autologin crash and restart containment

LuxeDM 1.4.1-1+luxe14 preserves Codex CLI's ShowSession guard: autologin
selects the configured session before the greeter windows exist, so drawing
must wait until a real root surface is available. Mirrored drawing also skips
missing surfaces. The live failure was X_ClearArea on window None, followed by
102 BadWindow exits and repeated display/input resets. The recovered desktop
already runs the manually patched executable; the package records the repair.

Set systemd StartLimitIntervalSec=60, StartLimitBurst=3 and RestartSec=3 to
contain future startup failures. An actual failing transient user service
verifies the policy stops retrying. Native private-X/PAM tests verify initial
selection, post-close selection, mirror resource lifetime and recovery. All
12 checks pass across the initial suite and corrected restart-policy rerun.
Autologin remains enabled and local conffiles are preserved; no desktop restart.

## 13 October 2026 — PCManager editable properties

PCManager 1.4.0+gtk4-4+luxe29+desktop-cairo3 adds owner/group/other Read,
Write and Execute/Access controls to Properties, alongside size and disk usage.
Apply permissions runs asynchronously and affects selected items only. Mixed
multi-selection permissions are retained unless edited; unchanged and special
bits are preserved from the current mode. Local owned regular files and folders
are editable; links, special files and remote locations are refused. A retained
native descriptor prevents a renamed/replaced path changing an unrelated item.
Close is neutral and discards uncommitted edits. Properties scrolls on small
screens. Seven tests pass, including permission changes and real properties UI.
DEB built for testing; no host installation. RPM tools unavailable locally.

## 13 October 2026 — panel preferences cleanup

Panel luxe45 removes the unused File Manager text entry from Advanced
preferences. The working file-manager dropdown and terminal setting remain.
Build only; no host installation.

## 12 October 2026 — slab without outline

slab 0.2.9~testing1 removes the thin dock border and theme shadow. Background
opacity remains configurable independently of icons and indicators.
Testing DEB only; no local installation.

## 12 October 2026 — menu shortcut and running slab

slab 0.2.8~testing1 adds a non-starting --reveal command and focus-preserving
D-Bus action. Panel luxe44 sends that action asynchronously when handling its
system-menu shortcut; absent slab is ignored with D-Bus auto-start disabled.
Existing menu binding is retained. Five slab tests and 18 panel tests pass.
Build only; install both testing packages and restart their existing processes.
Lumen's planned A/B UI selects classic bottom-panel versus top-panel/dock
layout, autostart and matching Super behavior; public slab release still gated.

## 12 October 2026 — slab hidden geometry fix

slab 0.2.7~testing1 retains native window dimensions when hidden so GTK's
zero allocation cannot shrink/recenter the reveal strip during model refresh.
Local read-only inspection found the running pre-upgrade executable deleted
on disk and a 1px-wide reveal window. Installing does not restart slab: use
Settings Stop then Start after installing. Added hidden-refresh geometry test.

## 12 October 2026 — slab autohide reliability

slab 0.2.6~testing1 enlarges the scale-aware reveal strip, re-raises it on
stacking changes and keeps the dock visible across the bottom gap. Openbox
regressions cover prolonged edge dwell and repeated reveal/hide cycles.
No hidden polling or focus stealing; testing DEB only, not installed.

## 12 October 2026 — slab ring indicators

slab 0.2.5~testing1 replaces numeric badges with larger theme-colored dots in
a ring around each icon, one per open window. Active groups get larger,
contrasting dots. Background-only opacity is retained. Isolated rendering
coverage checks the two-window ring. Testing only, not installed locally.

## 12 October 2026 — slab background-only opacity

slab 0.2.4~testing1 applies alpha to the themed background rather than the
whole window, keeping icons and indicators fully opaque. Existing saved opacity
values are preserved. Testing DEB only; no host installation.

## 12 October 2026 — slab presentation controls

slab 0.2.3~testing1 keeps the dock free of management/About controls. Separate
Settings owns live spacing, padding, margin, opacity, optional GTK theme override
and short hover transitions (off by default). Opacity requires compositing.
Four isolated tests pass, including live geometry/opacity and theme reset.
Testing-only DEB; no host install or public release-matrix change.

## 12 October 2026 — compact slab and separate Settings

Build slab 0.2.2~testing1: smaller 20px default icons and spacing, overlaid
window counts, no in-dock settings control. The testing DEB includes slab and
slab Settings launchers, persistent sizing/autohide, start/stop and pin management.
Both follow desktop GTK/icon themes and color scheme live. Four isolated tests
pass, including settings bindings and remote stop; desktop files and package
payload validated. Previous DEB retained as rollback; no host installation.

## 12 October 2026 — slab testing DEB

Advance slab to 0.2.1~testing2 and add standalone CPack DEB packaging,
automatic shared-library dependencies, a menu launcher and license notice.
No autostart, installation on the host, or release-matrix addition.
Normalize payload permissions with the shared packaging helper; the initial
testing1 build was withheld after validation found restrictive directories.

## 12 October 2026 — slab experimental prototype

Advance slab to 0.2.0-testing: grouped application icons and running counts,
window cycling and keyboard-accessible menus, persistent pin/unpin/reorder,
validated installed-launcher drops, and delayed autohide with non-focusing
edge reveal. Keep widgets on activation/count updates, cache desktop metadata,
and dispatch already-buffered X events without polling. Use managed root-space
placement for Openbox and scale-aware sizing. Add pin/model and isolated
Openbox gesture/focus/autohide tests. Still no install, DEB, or distribution job.

Add a standalone GTK4/Cairo Plank-like dock under slab, with desktop-file
launchers, event-driven X11 running-window buttons and theme-native controls.
Keep it outside installation, autostart, package generation and release CI.
This is an early prototype, not a complete Plank replacement or Wayland backend.

## 9 October 2026 — monorepo CI and Ubuntu compatibility candidates

Add independent package jobs for Ubuntu 24.04 and Debian 13, source-hygiene
checks, pinned APT build inputs, per-job provenance, and complete-cohort audits.
Keep generated output out of package directories; move the old Settings
comparison to tools/diagnostics. Initialize local main without committing or
uploading. Existing current/rollback DEBs remain untouched.

Lucid luxe12 accepts Noble's libspelling 0.2 with tested spell-check behavior.
Panel luxe43 and LuxUp luxe13 use GTK4.14-compatible icon flags.
LuxeDM luxe13 derives JPEG/PNG and other ELF dependencies from the target's
native library metadata instead of requiring Debian's JPEG package on Ubuntu.
Fix isolated
test inputs for Appearance, polkit and the Panel; include real GVfs/GTK probe
dependencies and a private sound fixture. Validation and remaining limits are
recorded in ci/README.md and astra.md; these are unpromoted CI candidates.

## 9 October 2026 — LuxuryStore catalogue RAM, 0.1.0-24

APT rows no longer embed unused Flatpak/Snap metadata. Successful catalogue
replacement releases loader/pointer-vector storage before one optional glibc
heap trim; current rows/cache/marks and failed-load fallback stay available.
Disabled repository Remove actions are ignored. Matched eight-reload workloads
retain about 84 MiB less anonymous memory, with peak RSS 37–39 MiB lower;
reload means increase by roughly 0.1–0.3 seconds. This is Store active-use RAM,
with desktop idle acceptance still open.

Fresh Release build and all eight CTests pass, including both APT ABIs, mocked
Flatpak handoffs and new repository-row GTK coverage. Audited DEB promoted;
24 current checksums/ownership and 24 frozen rollback hashes verify. RPM build
attempted but rpmbuild is absent. No install/live transaction. Details:
`artifacts/STORE-CATALOGUE-MEMORY.md`.

## 9 October 2026 — release directory cleanup

Jerry reports approximately 511 MiB used RAM on the Chromebook and prioritizes
release preparation. Move 92 superseded builds, one crash dump and ten bytecode
caches to desktop Trash (~3.33 GiB); retain current builds, RAM evidence, active
Store work, build inputs and the complete current/frozen 24-package sets.
Save source/artifact snapshot and 777 older-build evidence files; add root and
artifact indexes plus an exact recovery map. Checksums, manifest, all current
payload audits and package ownership pass. No application source, package
payload, installed system or version changed by this cleanup. Details:
`artifacts/cleanup-2026-10-09/README.md`.

## 9 October 2026 — CLI RAM attribution and real-menu profiling

Obtained active root-inclusive process/slab/DRM and XRes captures: resident
Luxe core ~50 MiB PSS, Xorg ~121 MiB and LuxeDM ~4 MiB. Expanded the read-only
diagnostic to distinguish kernel threads/zombies, inspect every readable DRM
client and expose slab-counter reconciliation gaps. The current exported-cache
estimate leaves 114.375 MiB of SUnreclaim unexplained; the original idle target
remains open. Fixed XRes client-zero wildcard PID attribution.

Repaired Panel's diagnostic sandbox device/proc mounts and private menu-helper
setup. Eight hidden full-profile runs require a searchable, launchable generated
catalogue sentinel and retain sound/battery. After ten menu cycles, Adwaita-icon
PSS is ~18 MiB and current Luxe-Dark-icon PSS ~22 MiB; memory settles without
growth from cycles five to ten. No production RAM-saving claim, package payload
change, version bump, installation or live desktop restart. Evidence and remaining
work: `shortage.md`, `build/shortage-search/cli-*.json`.

## 9 October 2026 — LuxuryStore completed Close action

- LuxuryStore 0.1.0-23 / RPM metadata 23: successful transactions style Close
  with GTK's native `destructive-action`, matching DebInstall and all six
  palettes. No new CSS/provider, timer or resident allocation. Running work
  remains uncloseable; failure retains neutral Close and error details.
- Eight CTests pass, including actual harmless VTE success/failure commands
  in both APT-6/7 backends. Verify disabled/uncolored while running and
  enabled/red only on success. Private Tokyo Night screenshot inspected.
  Existing test-only GError reuse warning remains recorded, not suppressed.
- Build/publish only LuxuryStore; preserve frozen rollback. No installs,
  real package transactions, kernel/configuration/service changes or RPM
  runtime acceptance. LuxUp hand-off confirmed working by Jerry; reported
  >1 GiB whole-system idle RAM remains unresolved, investigated in shortage.md.

## 8 October 2026 — wallpaper repaint loop and update hand-off

- Frame 0.1.0-1+luxe23: correct luxe20 regression repeatedly subscribing to
  RandR after display restore. Subscribe once, update Xlib configuration and
  repaint only for changed actual root dimensions; keep early/queued-event
  handling and original-resolution GTK4/Cairo rendering. No idle timer.
  Add documented re-selection notification fixture (luxe20 fails), same-size
  event burst, stable cache/CPU checks after real private resize. Four final
  tests pass at 720p/4K and three repeats. Final 18-image cycles: maximum
  3277/3276 KiB retained PSS, zero settled idle CPU, sources unchanged.
- LuxuryStore 0.1.0-22: keep LuxUp's existing --mark-updates protocol; re-read
  APT lists on warm hand-off, resume pending requests after busy resolution,
  refresh already-enabled store sources and reset automatic-mark state.
  Preserve previously marked APT/store selections and authorization gates.
  Eight CTests pass, both file-only APT-6/7 fixtures pass five repeats for
  cold/warm/busy hand-offs and known upgrade plans. No real source addition,
  transactions or native Snap acceptance. LuxUp unchanged/not rebuilt.
- Publish/audit only these two DEBs; 24 current ownership/checksums and frozen
  24 rollback hashes pass. Superseded current artifacts replaced, source build
  outputs recoverable. Trial versions advanced during regression strengthening;
  only final Frame23/Store22 promoted. RPM unavailable. Nothing installed or
  restarted. Whole-system idle RAM acceptance remains pending user capture.
- Independent Victus APT blocker reproduced without mutation: proposed kernel
  image needs linux-base >=4.17~, while APT selects installed 4.12.1. Combined
  kernel update simulation fails. No automatic repository/pinning/kernel fix.
  Full report/install list: artifacts/WALLPAPER-UPDATE-HANDOFF-FIX.md.

## 8 October 2026 — wallpaper cropped after display layout changes

- Frame 0.1.0-1+luxe20: fix a stale 3200x1080 root pixmap/cache observed on
  Jerry's 1280x720 desktop. Subscribe before first paint and handle resize
  events already buffered in Xlib, not only readable socket notifications.
  Repaint at current root dimensions; original 4K wallpapers stay untouched.
- Preserve GTK4/Cairo, viewer improvements, stretch, private lossless PNG level
  7 cache and disposable upload connection. No idle polling or new daemon.
  Regression reproduces the old FD-only failure; real 4K landmark scaling,
  720p/4K CTests, five 3200x1080 repeats and both 18-image collection cycles pass.
  Wallpaper retained PSS stays below 3 MiB with zero settled idle CPU ticks.
- Build/audit/publish only Frame; unpublished trial luxe19 advanced when tests
  were strengthened. Current 24 DEBs and frozen 24 rollback checksums/ownership
  pass, rollback bytes unchanged. Superseded current copy replaced, build output
  recoverable. RPM unavailable without rpmbuild. Nothing installed/restarted.
  Install/evidence: artifacts/WALLPAPER-RESIZE-FIX.md.

## 8 October 2026 — continued viewer RAM, paused media CPU and regression work

- Frame 0.1.0-1+luxe18 / RPM 1.luxe18: keep one immutable Cairo display
  surface, release decoded pixbufs before GTK presentation, share storage
  through rotation/flip matrices, permit fit shrinking and trim freed heap
  once after paint on glibc. Keep GTK4/Cairo, image source bytes, full display
  cap, WebP/EXIF/alpha, gestures/zoom/navigation/slideshow and wallpaper path.
  Strengthen resize test with real pixels and add snapshot/transform/ownership
  checks. Unpublished luxe17 retention/resize trial corrected before shipping.
- Six-palette real 4K viewer profiles: 26–34 MiB less anonymous memory displayed,
  50–66 MiB less after four rotations; return to small image releases storage.
  Twelve three-second candidate idle intervals record zero ticks. No system
  RAM cap, universal peak, long-term power or native Wayland claim. Both actual
  18-wallpaper cycles at 720p/4K retain under 3 MiB Frame PSS and clean up.
- Matinee 0.1.0-1+luxe14 / RPM 1.luxe14: paused workers wait on a condition,
  broadcast on Play/EOF/cancel before joins. Active timing, audio, visuals and
  packet ownership unchanged. Eleven internal cases and five repeats pass,
  including direct blocked EOF/Stop and zero paused-video voluntary switches
  during 200 ms. Extra wakeup coverage advances unpublished luxe13. Unsafe
  naive A/V queue backpressure not shipped; no RAM/active-throughput claim.
- LuxeSettings 0.1.0+luxe12 / RPM 1.luxe12: fix launcher fixture race reading
  a created-but-unwritten marker. Wait for complete exact argv under bounded
  deadline; twenty repetitions pass. No production launcher behavior change.
- Independently build/test/audit/publish three DEBs. Full current regression
  116 CTests pass plus Frame's three 4K runs; DebInstall sandbox staging check
  rerun with /var/tmp permission. Full cohort file ownership/checksums and 24
  frozen rollback hashes preserved. Superseded current DEBs replaced, build
  outputs retained. RPM unavailable without rpmbuild; nothing installed or
  restarted. Session luxe42 wallpaper package retained, not rebuilt.
- Details and exact install list: artifacts/ACTIVE-IMAGE-CPU-PASS.md.

## 8 October 2026 — Frame upload-cache lifecycle and combined RAM/wallpaper handoff

- Frame luxe16 keeps final pixmap/RandR ownership on the wallpaper monitor,
  paints through a scoped separate X11 connection, then synchronizes and closes
  that connection. Cairo upload/shared-memory caches no longer remain resident
  after painting. Preserve GTK4/Cairo viewer, source bytes, lossless PNG level 7,
  stretch/full resolution, alpha, WebP, rotation, resize and prior error behavior.
- Private 720p/4K roots and three PNG source sizes compare preserved luxe15
  executable against luxe16. Third-repeat Frame PSS is 2.4–2.6 MiB, versus
  10.6–76.9 MiB before; anonymous saving depends on source/root size. 4K stress
  performs 25 replacements per source with bounded XRes pixmaps and no retained
  upload SHM. Processing memory peaks are broadly unchanged; this is not a
  verified Chromebook whole-system result or universal peak/CPU guarantee.
- All 18 actual supplied PNGs also pass rendering/selection/cache/stop checks
  at 720p and 4K below 3 MiB Frame PSS; three-second idle checks record zero
  CPU ticks. No source bytes changed. Full current regression: 116 CTests pass
  plus three additional Frame 4K test executions.
- Build and publish frame 0.1.0-1+luxe16, RPM release 1.luxe16. Frame tests pass
  at normal and 4K roots. Current native/session/shared automated suites pass;
  DEB payload/ELF/version and complete 24-package ownership/checksum audits pass.
  RPM generation remains unavailable without rpmbuild. Nothing installed or
  restarted; preserve all 24 frozen rollback packages byte-for-byte.
- Handoff also includes already-published LuxeSession luxe42 with 18 optional
  Horizon/River/Veil 4K PNGs and six original defaults. Rechecked actual-DEB
  supplied hashes/dimensions; no recompression/preloading or default replacement.
  Full current install list and evidence: artifacts/RAM-WALLPAPER-HANDOFF.md.

## 5 October 2026 — wallpaper collections published in LuxeSession luxe42

- Add Jerry's Horizon, River and Veil sets, each with six palette variants:
  18 lossless 3840x2160 RGB PNGs copied byte-identically from Downloads/wp.
  Install through LuxeSession under backgrounds/luxe/horizon, river and veil;
  preserve six original default/theme-follow wallpapers and custom selections.
- Add source hashes/provenance and staged-install test that decodes all 18
  installed images, verifies dimensions/modes and exact original bytes. No
  eager loading, new daemon, runtime dependency or wallpaper recompression.
- Temporary-DESTDIR install/decode test passed; all 18 source copies also
  compare byte-identically with Jerry's supplied files.
- Built `luxesession 0.5.5+gtk4-4+luxe42` independently, RPM release 5.luxe42.
  All 15 registered CTests pass; actual DEB holds 18 unchanged supplied PNGs
  plus six byte-identical generated defaults, unchanged /etc defaults and
  eight clean ELF payloads. Full 24-package ownership audit passes.
- Published the new DEB in artifacts/debs, replaced the superseded luxe41
  current artifact, and preserved all 24 frozen rollback hashes. Current
  checksums pass; no package installation/session restart. RPM unavailable
  because rpmbuild is missing. Installed size grows by 8421 KiB; no preload.
- Details: artifacts/WALLPAPER-COLLECTIONS.md. Choose images in Frame; a
  collection directory rotates all six colors, without automatic theme filtering.

## 5 October 2026 — portable GTK4/Cairo active-memory pass

- Extend Panel's process-local unused GL/Vulkan suppression to the shared
  native initializer. Keep GTK4/Cairo, caller flags and exact child environment;
  no session-wide override. Panel drops its duplicate implementation without
  claiming another Panel saving. Headless brokers remain headless.
- DebInstall sets runtime-compatible flags before importing PyGObject's GTK
  overrides; import itself was starting GTK too early. Test actual GL refusal,
  repeated Cairo surfaces, unset/empty and inverted-all flags and child values.
- Frame returns freed glibc heap once after wallpaper processing, retaining
  originals, lossless PNG compression 7, 4K, RandR, rotation and pixmaps. Cold
  720p/4K-source heap saves 3.5 MiB; repeated 720p barely changes. 4K-root/4K-source
  retained anonymous falls 31.6 MiB with nearly unchanged operation peak.
- Private actual four-app workload across six palettes, two alternating repeats:
  93.2–98.3 MiB less anonymous RAM and 106.2–116.4 MiB less subtotal PSS. Excludes
  Xorg/helpers; not a whole-system/hardware under-400 MiB claim. CPU intervals
  after eight-second settling record zero ticks in both cohorts; no CPU-speed
  claim. All six captures inspected, but overlapped windows limit visual coverage.
- Fresh independent builds and version bumps for all 21 affected GUI packages.
  Package/version/test completion table: astra.md and
  artifacts/PORTABLE-MEMORY-PASS.md. 97 component and seven shared CTests pass;
  additional 4K wallpaper behavior test passes. 49 ELF payloads and complete
  24-package ownership audit pass, including Panel sound/battery. No adwaita
  dependency/linkage, RPATH or public private-library payload. RPM unavailable.
- No installation, SSH mutation or restart. Networking, compositor defaults,
  kernel and availability reserves unchanged; frozen rollback preserved.
- Side request: provide artifacts/wallpaper-kit/luxe-tokyo-night-4k.png and
  palettes.md with all six implemented color sets and Light's wallpaper tones.


## 4 October 2026 — release inactive LuxeDM greeter graphics

- `luxedm 1.4.1-1+luxe12`: free decoded images after pixmap preparation and
  release closed primary/mirror greeter pixmaps in the parent at successful
  desktop-session fork. Keep PAM conversation context, user/session selection
  and fonts alive. Recreate graphics/input if a later prompt or message needs
  them; repeated close/release is safe. Mapped greeters and lockers are excluded.
- Return freed glibc heap pages once at handoff; no idle timer, global allocator
  setting, PAM policy, service configuration or kernel change. Private 1366x768
  test saves 7.6 MiB anonymous and removes greeter pixmaps; 4K saves 28.4 MiB
  anonymous. Recovery has re-decode/allocator costs; hardware totals pending.
- Eleven CTests pass, including real XRes/Panel lifetime and the production
  PAM conversation callback, mirrored pixels, input grabs/recovery, password
  clearing and lock exclusion. Full PAM/boot/logout/lock hardware testing remains.
  DEB/ELF/version/conffiles/cohort ownership/checksums pass; current/frozen sets
  remain 24 packages each, rollback hashes unchanged. RPM unavailable. Nothing
  installed or restarted. Evidence: artifacts/LUXEDM-GREETER-MEMORY.md.

## 4 October 2026 — resolution-sized lossless wallpaper PNG

- `frame 0.1.0-1+luxe14`: stretch into the desktop's actual raster before X11
  upload, retaining full 4K output on 4K displays and original source files.
  RandR changes regenerate from the original; directory/login/daily rotation,
  WebP/EXIF and transparency remain. No GTK startup in the wallpaper service.
- Keep one atomic private PNG derivative, compression level 7, instead of
  accumulated copies. GdkPixbuf provides the encoder/resampler; no ImageMagick
  dependency. Cache failures do not prevent painting; unsafe cache targets are
  refused, and selecting the cache itself never overwrites that selected source.
- Private 1280x720 fixture with a 4K source: shared transfer storage falls by
  about 28 MiB across Frame/Xorg; cold anonymous rises ~3.6 MiB, while repeated
  applies retain ~28 MiB less anonymous memory. Apply is slower due to CPU
  resampling/PNG encoding; no added idle polling. Hardware acceptance pending.
- Three CTests pass, including real nested-display resize, 4K output, cache
  bounds/alpha/WebP/pixels, unchanged originals and fallback behavior. DEB,
  ELF/desktop/schema/ownership checks pass. RPM unavailable (no rpmbuild).
  Current cohort 24 DEBs; frozen 24-package rollback unchanged. Not installed.
  Evidence: artifacts/FRAME-WALLPAPER-MEMORY.md.
- Hardware follow-up at 22:36: free-used 546 -> 529 MiB; root process PSS
  273.1 -> 247.1 MiB. Matching Frame/Xorg shared-map reductions corroborate
  ~27.6 MiB less transfer storage. Not a controlled whole-system attribution;
  Frame version omitted by the report and historical target remains open.

## 4 October 2026 — panel avoids unused graphics initialization

- `luxepanel 0.10.1+gtk4-8+luxe41`: initialize native GTK4/Cairo without its
  unused GL/Vulkan paths. Restore original GDK/GSK flags before launching apps;
  preserve unset/empty values and inverted `all` syntax. Use runtime-compatible
  switches for GTK 4.14 and 4.16+. Other packages and session settings unchanged.
- Profile libfm stage costs rather than removing unproven dependencies: complete
  headless initialization adds less than 0.3 MiB anonymous memory. The large
  saving is graphics initialization, not palette CSS or an icon-format change.
- Seven private-display panel profiles, cold GTK probes, repeated menus and
  actual patched panel comparisons show roughly 18 MiB less anonymous RAM.
  No claim that the under-200-MiB system target is restored; hardware acceptance
  and the remaining whole-system attribution are still open.
- Nineteen CTests pass: environment inheritance/recreated Cairo windows, six
  palette/outline/tray checks, keyboard recovery, taskbar, run-dialog, volume,
  plugin/configuration and X11 boundaries. Inspect all six palette captures.
  Package handoff: artifacts/PANEL-GRAPHICS-MEMORY.md. No live install/restart;
  preserve the frozen rollback generation.
- Record optional classic/top-panel dock distro presets and the proposed GTK4
  Plank-derived dock in DISTRO.md; no dock implementation or Wayland guarantee.

## 4 October 2026 — transient Polkit password dialog

- `luxepolkit 0.1.0+gtk4-5+luxe16`: GTK-free registered session listener starts
  a temporary unprivileged GTK4/Cairo password child. Authority cookie/details/
  permitted identities use a bounded inherited pipe, never argv or files.
- Preserve existing trusted Polkit/PAM helper, retry, permitted-identity selection,
  buffer clearing, theme following, focus and X11 input grabs. No policy changes.
- Reap children on success/cancel/crash, force-stop stalled cancellation after
  two seconds and kill a child on listener death. Reject competing prompts,
  missing helpers, empty identities and malformed/oversized requests. Registration
  errors now log and exit nonzero rather than keeping a GTK error window resident.
- Four CTests pass with isolated displays/fake PAM; package modes, GTK-free
  listener linkage, version ordering and combined ownership checked before
  publication. Frozen rollback preserved; no installation/live-session edits.
- Real authentication and physical RAM/CPU acceptance remain user testing.


## 4 October 2026 — resident-memory batch

- Frame `0.1.0-1+luxe13`: X11 root wallpaper without GTK initialization in
  daemon mode; bounded image decoding, stretch, saved selection, daily/next-login
  directory rotation. Viewer and chooser remain GTK4/Cairo.
- PCManager `1.4.0+gtk4-4+luxe27+desktop-cairo3`: remove wallpaper renderer and
  forward legacy desktop commands to Frame before GTK initialization. Retired
  source/tests are recoverable in `/home/jerr/luxe-retired-wallpaper-20261004`.
- Appearance `0.3.0+gtk4-2+luxe22`, Settings `0.1.0+luxe10`, Session
  `0.5.5+gtk4-4+luxe40`: route wallpaper actions/defaults to Frame; migrate known
  defaults without replacing custom commands. Session D-Bus settings opens the chooser.
- LuxUp `0.1.1-1+luxe11`: GTK-free tray/check service and temporary GTK4/Cairo
  review window; preserve checks, cancellation and existing authentication flow.
- LuxeNotify `0.1.0-7`: GTK-free public broker with bounded requests and an
  authenticated temporary GTK4/Cairo renderer; release it after cards close,
  preserve notification IDs/replacement/counts and recover after child crashes.
- Seven DEBs: 36 component CTests, payload/linkage/version and 24-package
  ownership audits. No installation or live-user changes; frozen rollback unchanged.
  Physical RAM/CPU acceptance remains pending. See artifacts/RESIDENT-MEMORY-BATCH.md.


## 4 October 2026 — GTK4/Cairo suite completed for hardware testing

- Complete all 21 maintained GUI package migrations: native GTK4 application,
  window, preferences, status, About and dialog presentation, with process-local
  Cairo selection. Remove their obsolete libadwaita build/runtime dependencies
  and retired presentation branches. GTK4 stays; all six desktop palettes and
  third-party libadwaita color coverage stay. DM and two data packages are unchanged.
- Share repeated native-widget patterns in a private statically linked helper,
  not a new installed toolkit or an Adw emulation layer. Restore the original
  renderer environment before spawning children. Session/clipboard and idle
  Notify/Polkit registration retain their headless paths.
- Fix migration-specific lifecycle differences: Matinee disconnects window
  children before template disposal; native preference controls carry labels
  without pretending to be list rows; save/discard and cancellation dialogs
  retain semantic responses; native About dialogs retain upstream legal credits.
- Package versions and individual completion checks are recorded in astra.md
  and artifacts/GTK4-CAIRO-ROLLOUT.md. PCManager is
  `1.4.0+gtk4-4+luxe26+desktop-cairo2`: Debian's interpretation of the earlier
  hyphenated trial required retaining that suffix for a normal forward upgrade.
  No experimental renderer option remains. Settings is now `0.1.0+luxe9`.
- Pass all 84 component CTests, the shared native-control/dialog fixture and two
  retention tests. Audit all changed DEB payloads/dependencies, 46 ELF objects
  (no libadwaita or build RPATH/RUNPATH), forward rollback-version ordering and
  all 24 packages together for file-ownership collisions. Both sound and battery
  modules ship in Panel. Existing user panel profiles remain untouched.
- Capture and inspect all six native palette screenshots plus stock light/dark;
  final cold Settings comparisons use Cairo on both implementations. Measurements
  are application PSS samples, not proof of whole-desktop RAM savings.
- Publish the 24-package test cohort in artifacts/debs while preserving the
  24 frozen working DEBs in artifacts/old byte-for-byte. No installation, live
  restart or user configuration change. Chromebook idle RAM/CPU, physical-device
  behavior, Ubuntu 24.04 and Fedora acceptance remain outstanding.

## 4 October 2026 — GTK4/Cairo rollout, Settings first

- Promote the approved plain GTK4 presentation to production Settings
  `0.1.0+luxe8`, retaining the normal command, application ID, eleven launchers,
  theme following, About and modal launch errors. Retire the libadwaita
  presentation and prototype build branch.
- Prime Cairo before the first GUI surface using `shared/luxe-renderer.h` and
  restore the renderer environment before launching other applications. Both
  Settings CTests, payload/dependency and ELF checks pass; no libadwaita/RPATH.
  No installation or live desktop changes performed.
- Move 63 superseded DEBs (40.1 MiB) from validated old-package directories to
  recoverable desktop trash, retaining historical sources and the current set.
  Jerry subsequently requests one rollback generation: organization retains
  current plus one frozen working DEB per app, including unchanged apps.
  Explicit `--rotate-old` advances it after acceptance. Two retention/version-
  order tests pass.
- Record the approved suite-wide policy in the Bible and per-package plan in
  astra.md before migration. Only Settings is completed in this batch.

## 4 October 2026 — separate plain GTK4 Settings prototype

- Add `luxesettings-gtk4-prototype` version `0.1.0+luxe1`, built independently
  with CMake/CPack alongside the unchanged production Settings `0.1.0+luxe7`.
  Standard GTK4 window, header, grouped action rows, About and error dialogs
  share the existing eleven validated launchers. No libadwaita/Granite linkage.
- Reuse all six existing Default-based GTK palettes with scoped layout CSS.
  Correct prototype subtitle contrast; retain focus, activation, scrolling and
  live light/dark preference updates. No renderer or theme policy changes.
- Private behavioral fixture verifies eleven actual child argument vectors,
  duplicate activation, modal launch failure, About/license and scheme changes.
  Compare 64 fresh processes and capture all six palettes plus stock light/dark.
  Typical saving: 4–5 MiB PSS per open Settings window, not idle-desktop savings.
- Record method/results/limits in artifacts/GTK4-SETTINGS-PROTOTYPE.md, light.md
  and astra.md. Keep the side-by-side DEB in its experimental debs directory,
  outside the 24-package release set. No installation or live desktop changes.

## 3 October 2026 — isolated September 8 source-rebuild diagnostics

- Build all 14 available archived app/data components plus private dependencies
  into 15 side-by-side `luxe-sep08-*` DEBs, version `2026.09.08+rebuild2`.
  Output: `/home/jerr/luxe-baselines/september08/debs`; one prior rebuild1
  checkpoint per app in that baseline's `old`, not the production release dirs.
- Original application source remains byte-identical to archive (1,114 checked
  source/header/Vala files). Adapter supplies prefix/resource/build flags,
  compiled private Lucid schema, full XKB/Hotkey modules and package modes.
  Generated Store copy relocates its helper path only; no old privileged policy.
- Both staging and extracted-DEB tests pass core startup/settled idle CPU,
  eight GUI startups, all ten Panel types including volume/batt, and mocked
  Polkit startup. All 15 DEB/40 ELF provenance/linkage/ownership audits pass.
  Detailed per-component checks/limits: artifacts/SEPTEMBER-8-BASELINE.md.
- Add distinct supervised test login, README, checksums and read-only executable-
  identified measurement script. Archive lacks ten later apps; no full-feature
  or under-200 MiB claim. No old Store transactions or real auth/hardware sound/
  battery/locking validation. Current Panel configuration issue stays open.
- Jerry approves a separate Cairo comparison if this baseline only partially
  reduces RAM; preserve controls and reject per-app RAM/CPU/function regressions.
  Current app code/renderer policy unchanged pending hardware results.
- Preserve recovered September 30 inventory outside /tmp and both original
  archives. Recoverably relocate 91 inactive temp targets, clearing about
  317 MiB tmpfs; retain active/system/uncertain data. Nothing permanently
  deleted, installed, restarted, committed or pushed.

## 3 October 2026 — desktop Cairo Chromebook checkpoint (report only)

- Jerry confirms both Cairo trial packages installed; SSH capture reports
  616 MiB used versus the preceding terminal-closed 652 MiB. PCManager PSS
  is 8.6 MiB lower; the five-process PSS subtotal is only 2.0 MiB lower.
  Reported shared falls 40 MiB, but no new meminfo/DRM attribution accompanies
  this capture. Details/accounting limits in light.md and astra.md.
- The historical under-200 MiB baseline remains unresolved. No new source
  changes, rebuilds, installations, restarts or live settings changes here.

## 3 October 2026 — experimental desktop-only PCManager Cairo build

- Build pcmanager 1.4.0+gtk4-4+luxe24+desktop-cairo1 with the new
  OFF-by-default PCMANAGER_DESKTOP_CAIRO_TRIAL option. Only wallpaper windows
  use Cairo, including monitor rebuilds; browsers/preferences in the shared
  IPC process retain their existing renderer policy. No environment changes.
- Temporarily use GTK's private display renderer selector during synchronous
  desktop realization, preserving/restoring exact data and destroy ownership.
  This is implementation-dependent diagnostic code, not a supported setting.
- Trial and independent normal OFF builds each pass all 10 CTests, including
  real desktop/rebuilt desktop/browser renderers, child environment and data
  ownership plus existing IPC/stretch/file-operation/drag-drop coverage.
- A new fixture also reproduces an icon-view close/model teardown crash in
  both trial and normal controls. Logged separately in astra.md for diagnosis;
  this trial does not modify browser code or claim that close case passes.
- Private production PNG/stretch probes confirm OpenGL versus Cairo, with
  PSS 91,176 versus 61,661 KiB (single software-rendered startup samples).
  Hardware RAM/CPU/visual acceptance and the historical regression stay open.
- DEB payload/dependencies, desktop files and no-RPATH audit pass. RPM is
  blocked by missing rpmbuild. Retain luxe23 as sole rollback, move luxe22 to
  recoverable trash and refresh manifest/checksums. Installation/measurement
  instructions: artifacts/TEST-CAIRO-DESKTOP.md. No live install/restart/commit.
- Final promotion checks pass: all 24 current DEBs have no ownership conflicts
  and all 48 current/prior checksums verify. Normal luxe23 is the sole rollback.

## 3 October 2026 — experimental panel-only Cairo build

- At Jerry's request, build luxepanel 0.10.1+gtk4-8+luxe39+cairo1 using the
  opt-in LUXEPANEL_CAIRO_TRIAL option. It remains OFF for normal builds; no
  Session, PCManager, Polkit, LuxUp or global renderer change is made.
- Prime GTK's cached Cairo choice on an unmapped disposable surface and
  restore the original environment before application/plugin/helper launches.
  This is an experimental GTK implementation-dependent diagnostic, not a
  stable end-user renderer setting. Two real-native-window tests verify
  Cairo after restoration and child inheritance with original unset/ngl
  choices. Actual production-panel logging also confirms Cairo.
- All 15 Panel checks pass. Volume integration needs the host audio runtime;
  its initial outer-private-runtime failure passes in the correctly isolated
  host-audio fixture without a plugin change. Menu/theme/tray, keyboard,
  taskbar, restart/respawn, Run, config/property/icon/layout coverage passes.
- DEB payload/dependencies, packaged version and ELF no-RPATH checks pass,
  as do all 24 current-package ownership checks and both checksum lists.
  RPM attempted, blocked by missing rpmbuild. Promote the trial into debs;
  normal luxe38 is the sole old/rollback, and luxe37 moves to recoverable
  trash. Nothing installed, restarted or committed.
- Jerry confirms no compositor on the Chromebook, so compositor removal is
  not the next experiment there. Hardware RAM/CPU/visual acceptance remains
  open. Install/comparison/rollback instructions: artifacts/TEST-CAIRO-PANEL.md.

## 3 October 2026 — Appearance icon picker, luxe20

- Fix an enumeration regression that interpreted absent optional Hidden
  metadata as a malformed boolean and excluded normal installed packs,
  including Luxe icons. Debian's installed Adwaita is explicitly hidden;
  expose it as a stock-theme choice and retain valid active hidden themes.
  Other hidden fallback themes and malformed boolean values stay excluded.
- Real-dialog regression test first reproduces the missing-pack failure on
  old source. All three final Appearance CTests pass after the fix, including
  private system/user/legacy enumeration, duplicate precedence, display-name
  fallback, active selection and the production refresh callback. Existing
  GTK deprecation warnings remain known debt.
- Build and audit luxeappearance 0.3.0+gtk4-2+luxe20, including dependencies,
  desktop entry and no-RPATH payload. Current 24-package ownership audit and
  both checksum lists pass. Promote luxe20 into artifacts/debs; luxe19 is
  the sole prior in artifacts/old. Luxe18 moves to recoverable desktop trash.
  RPM attempted, blocked by missing rpmbuild. Nothing installed or committed.

## 3 October 2026 — Chromebook Adwaita checkpoint (diagnostics only)

- Jerry confirms the 820 MiB Adwaita result followed a reboot and only the GTK
  theme changed; icons/wallpaper were retained. High Contrast subsequently
  reports 660 MiB, without a matched workload/PSS capture or explicitly
  confirmed reboot state for that reading. These do not establish theme RAM
  causality. Details in light.md and astra.md; no live-desktop changes by Codex.

## 3 October 2026 — laptop update-history comparison (diagnostics only)

- Both machines now share a hostname; identify laptop/Chromebook explicitly. Laptop logs show GTK4, libadwaita, Mesa and procps unchanged since 29 August; libc/GLib/Xorg changed 12 September. No new toolkit-version switch is demonstrated during the claimed low-to-high week; runtime theming remains under investigation.
- Read-only laptop journals locate the first retained 7.1.13 boot at 28 September 22:16 EDT, following 7.1.8. The 7.1.8 kernel/initramfs remain installed, and installed kernel configs differ only in version/Rust compiler metadata. 7.0.13 was installed later but is not used by the later recorded boots. Kernel timing is a lead, not a diagnosis.
- Record official kernel-change review and a proposed matched-login old/current-kernel A/B in light.md and astra.md. Do not mix package cohorts or post-auth states. No reboot, bootloader/config change, package downgrade, release build or claim that sub-200 MiB idle RAM is restored.

## 3 October 2026 — graphics ownership, post-auth retention and Debian update check

- Subsequent clarification: the Chromebook is freshly installed that afternoon, not an in-place upgrade of the historical low-memory baseline. Distro versions may differ between installations; current dpkg upgrade history cannot prove the older baseline or update causality. No earlier installed kernel is assumed.
- Resolve Chromebook process names and service versions: Polkit retains 54.7 MiB PSS after first authentication; LuxUp's GTK tray is 40.0 MiB. The five largest named processes total 258.4 MiB of the prior 366.1 MiB process PSS scan. No running LuxeTerm appears in the follow-up list.
- Intel's three debugfs aliases represent one 101.7 MiB shrinkable-object aggregate. Per-client fdinfo establishes hardware i915 usage and graphics-buffer ownership; repeated descriptors and shared objects must not be counted twice. Detailed per-client values and accounting limits are recorded in `light.md`.
- Add a diagnostic-only production-listener/mocked-session memory probe: six dialogs finalize with no surviving GtkWindows under nested software GL and Cairo, while toolkit/graphics/allocator storage persists after first use. Diagnostic trimming occurs only in the disposable helper, not production. Short-run plateau is not long soak or Chromebook acceptance.
- Review official Debian 13.5–13.7 summaries and the 29 September 6.12.111 security upload. The reported Chromebook kernel is newer than the 12 September 13.7 point release; update timing is a plausible lead, not a demonstrated cause. Request dated dpkg old/new-version history before a controlled kernel comparison. Primary references and findings are in `light.md` and `astra.md`. No new release builds, installations, downgrades, live configuration changes or baseline-restored claim.

## 3 October 2026 — RAM investigation checkpoint (diagnostics only)

- Follow-up zone/meminfo data reconstructs Chromebook MemAvailable within 2 KiB. About 215 MiB of the 714 MiB reported used is the availability reserve/reclaim margin, not allocated Luxe memory; roughly 499 MiB remains in that decomposition. Shmem/Unevictable ~127/~126 MiB suggest an Intel GEM-buffer attribution check, not a proven graphics leak. Optional existing GEM/fdinfo reporting is added without mounting debugfs or changing kernel policy. Whole-process PSS and confirmed package cohort remain pending.
- Chromebook Panel/PCManager/Xorg combined PSS is 172.4 MiB against 716 MiB reported used; all-process/kernel attribution and package/reboot state remain pending. Record Intel HD Graphics 500 and kernel watermarks without changing them.
- Extend private memory probes with resident mapping-class PSS, file/shmem/swap/huge-page fields and selected-renderer logs. Confirm nested Xephyr uses software GL and cannot present Vulkan; do not generalize these renderer comparisons to Chromebook hardware. The isolated panel still lacks its volume plugin. Final private PNG/stretch default-vs-Cairo samples and bounds are in `light.md`.
- Extend the read-only idle report with process PSS totals, graphics identity, zone watermarks and THP state. Shell/Python checks and private diagnostic runs pass. Mark the handbook's old 122–125 MB claim as historical rather than current. No new DEBs, production edits, installs, renderer-policy/sysctl changes or cache dropping; the under-200 MB target remains open.

## 3 October 2026 — six PNG wallpapers and full-screen stretch

- `luxesession 0.5.5+gtk4-4+luxe37`: render the existing six wallpaper SVG sources into lossless 3840×2160 PNGs at build time using rsvg-convert. Only PNGs ship; vector build sources and a new runtime service are not installed. Set deterministic 0644 image modes. Encoded wallpaper content is about 2.9 MiB, an intentional disk tradeoff.
- `luxeappearance 0.3.0+gtk4-2+luxe19`: use PNG preset paths and pass stretch to PCManager, retaining the optional wallpaper-follow choice. `pcmanager 1.4.0+gtk4-4+luxe23`: use Tokyo Night PNG/stretch as the fresh profile default. Stretch fills mismatched aspect ratios but can distort the motif rather than letterbox it.
- Session's bounded, atomic, idempotent login migration changes exact retired bundled SVG paths and their former fit/crop modes, retaining custom wallpaper files, other custom modes, comments, unrelated settings and wallpaper-follow state. It does not change the running user's configuration during this work.
- Theme construction comparison finds shared Adwaita/Default resource bases and identical normalized widget-rule structure across all six palettes, not four additional theme engines. Broader theming/renderer costs and the historical RAM regression remain under investigation. A same-host private PNG probe saves about 6.8 MiB PSS with the default renderer and 0.3 MiB with Cairo; it does not explain hundreds of MiB or establish hardware idle acceptance.
- Fresh final suites pass 26 checks: Session 15, Appearance 3 and PCManager 8. Coverage includes six native GTK3/GTK4/libadwaita fixtures, real PNG decoding/dimensions, Appearance argv, migration safety/idempotence and actual wallpaper painting at 1366×768, 1280×800 and 1200×800. DEB payload and staged no-RPATH checks pass, as do ownership checks across all 24 current DEBs and both artifact checksum lists.
- Promote the three new DEBs into `artifacts/debs`, retaining Session luxe35, Appearance luxe18 and PCManager luxe22 as sole priors. Older superseded copies and the unpromoted Session luxe36 prototype moved to recoverable trash. RPM attempts are blocked by absent rpmbuild. Nothing installed or committed. The twelve-package handoff and remaining RAM acceptance gates are in `artifacts/INSTALL-RESOURCE-PASS.md` and `light.md`.

## 3 October 2026 — single-key shortcuts, icon ownership and regression evidence

- `luxehotkey 0.1.0+gtk4-2+luxe10`: remove the GUI's bare-letter restriction and reserved Tab/Escape/Backspace capture behaviors. Capture-phase controllers accept named keys; modifier taps commit on release, ignoring their own modifier bit while preserving chords and cancelling pending capture on focus loss. Explicit Clear buttons retain shortcut removal. Four CTests pass, including GUI-controller capture, bare Search/Super/modifier/ordinary keys, unchanged combinations, cancellation, CLI validation, and real Openbox single-key persistence. Chromebook physical-key retesting remains pending.
- `luxe-icons 2026.09.09-7`: fix the confirmed dpkg ownership collision with LuxUp. Only LuxUp owns its hicolor fallback; all Luxe/Light/Dark artwork, aliases and unique-name coverage remain. One redundant hicolor path is removed, not any theme icon. Two CTests and additional GTK3 lookup coverage pass. Real private dpkg roots reproduce the revision-6 overwrite failure and successfully unpack revision 7/LuxUp in either order. Added a cohort ownership audit, since individual payload audits miss cross-package collisions. No extra RAM saving is attributed to this packaging fix.
- Both changed DEBs are built, payload-audited and promoted; Hotkey luxe9 and Icons revision 6 are sole priors. Hotkey luxe8 and Icons revision 5 moved to recoverable trash. RPM attempts remain blocked by missing `rpmbuild`; no installed packages, live settings or commits changed. The handoff now includes eleven changed packages.
- Final 24-DEB cohort ownership audit and both checksum lists pass. All 56,069 remaining SVG paths are byte-identical to Icons revision 6; the sole removed SVG path is LuxUp's app-owned hicolor fallback.
- Updated `DISTRO.md`: omit ModemManager from the default Lumen image, retain NetworkManager for Wi-Fi/wired, and leave cellular support optional. This does not remove it from running systems.
- Continued RAM diagnosis: the Chromebook's supplied current cohort shows headless services taking effect, but Panel/PCManager/Xorg PSS and kernel watermarks were not obtained because the pasted command concatenated shell prose into `cat`. Its meminfo exposes a ~287 MiB gap between Total−Available and a cache-subtracted calculation; that is not a measured application allocation or a proven explanation of the historical regression. Isolated older/current Panel and PCManager probes are nearly identical; renderer differences are larger than palette/SVG differences. Exact evidence, fixture corrections and remaining hardware checks are in `light.md`; the under-200 MB target is not achieved or ruled out.

## 3 October 2026 — xcompmgr trial, luxesession luxe35

- At Jerry's request, select `xcompmgr -n` instead of Picom: simple client compositing without extra shadows/fades, one XDG conffile entry and explicit DEB/RPM dependency. Retire the shipped Picom config; existing personal settings are not erased and installing Session does not uninstall another package or stop a live compositor.
- After legacy conversion, migration scopes direct Picom autostarts out of X-Luxe using per-user overrides, preserving Exec arguments, other desktop restrictions, disabled choices, system files and unrelated applications. Links/special files are refused; repeated migration is idempotent. Custom shell wrappers remain a manual boundary.
- Fresh-source Release build and all 15 Session CTests pass, including real compositor ownership through the production XDG reader, no competing Picom start in Luxe, and retained Picom applicability outside Luxe. For this test only, Debian's xcompmgr tool was downloaded/extracted into private temporary staging, not installed or used as a Luxe build input. DEB audit/dependency/conffile/desktop/no-RPATH checks pass. RPM attempted, blocked by missing `rpmbuild`.
- The ten-package installation handoff is `artifacts/INSTALL-RESOURCE-PASS.md`. Session luxe35 replaces luxe34 as current; luxe34 is the sole prior, and luxe33 is recoverably trashed. No installation, commits or running desktop changes. Compositor visual behavior and whole-system RAM savings require hardware retesting; no vsync/effects equivalence or reduction is asserted.

## 3 October 2026 — all-package resource review and ten-package test cohort

- Reviewed RAM, CPU and disk decisions for all 24 packages in `light.md`; fresh-source component suites passed 91 CTest checks, counting each final suite once. Unchanged payloads were not rebuilt as redundant release DEBs. This completes the local review/build cohort, not all proposed stress tests or the whole-system RAM acceptance.
- Current changed packages: Session luxe34, Notify revision 5, Polkit luxe14, LuxUp luxe9, PCManager luxe22, Frame luxe11, Matinee luxe10, Power revision 7, Panel luxe38 and Icons revision 6. Each passed its DEB payload audit. Both artifact directories' checksums pass; exactly one current and one previous DEB per package remain. Superseded older copies were moved to recoverable trash. RPM attempts are blocked by missing `rpmbuild`; nothing installed or committed.
- Matinee luxe10 eliminates the duplicate full RGBA video frame: two 4K silent-video probes show approximately 30 MiB lower PSS/peak memory, retaining resolution, timing and immutable texture ownership. Its idle/video tick removal remains included. Frame retains scaled WebP decoding and event-driven refitting.
- PCManager luxe22 fixes an external-Move cancellable reference ownership error reproduced under GDB. Previously skipped drag/drop coverage now runs on a hidden private display with disposable HOME/XDG and fatal GLib criticals; all seven tests pass. Browser deferral alone does not show a reliable RAM saving.
- Panel luxe38 constructs menu rows only when opened, preserving first keyboard invocation and category state. All 13 checks pass; isolated startup saving is modest. Power revision 7 removes disabled-lock polling and safely re-arms exactly one enabled timer; four checks pass.
- Icons revision 6 links exact duplicate artwork internally: all 56,070 SVG paths match the prior bytes, with cached GTK3/GTK4 lookup coverage intact. Regular content falls ~62.7%; DEB size falls ~49.2% (12,502,052 → 6,356,458 bytes). No new RAM saving is attributed to deduplication; CPack Installed-Size is not a trustworthy linked-file disk measurement.
- A settled 72-change six-palette fixture plateaus near 54–55 MiB anonymous memory after warm-up, not proof of prolonged application leak freedom. Headless Session/clipboard idle probes show no CPU ticks over two seconds. Hardware acceptance, first-use authority readiness, GTK large-text clipboard conversion/transfer-bound parity and target-toolkit testing stay explicitly open. Added read-only `artifacts/measure-idle.sh` for post-install Chromebook accounting; the historical under-200 MB objective remains unverified.

## 2 October 2026 — stationary-window CPU and audited build handoff

- Matinee `0.1.0-1+luxe9`: visualization ticks only for audio visualizations; seek timer removed while idle/stopped. Behavior suite passes, including audio/video and timer transitions.
- Frame `0.1.0-1+luxe11`: retain native scaled WebP decoding and replace continuous fit-check ticks with completed-layout observation. Real pre-map load/resize and settled-frame tests pass, alongside viewer/WebP coverage.
- Seven optimization DEBs (Session, Notify, Polkit, LuxUp, PCManager, Frame and Matinee) passed payload audits and were promoted into `artifacts/debs`. One previous version per package remains in `artifacts/old`; superseded older DEBs were moved to recoverable desktop trash. RPM attempts cannot proceed without `rpmbuild`. No live installation or commits; whole-system acceptance and remaining resource work stay open in `light.md`.

## 2 October 2026 — lightweight startup and WebP RAM candidates

- luxesession `0.5.5+gtk4-4+luxe34`: headless Session avoids GTK initialization while keeping runtime bridge version selection; clipboard becomes GLib/Xlib-only with one selection-owning/event-reading connection, scoped error handling, bounded X property reads, ordered MULTIPLE replies and serialized source transfers. Fourteen tests pass; Session/clipboard fresh probe PSS falls to ~2.5/~0.7 MiB. Large GTK value-backed multi-encoding text still exposes a converter-stream deadlock; large raw UTF-8 byte-provider transfers pass and that limitation remains explicitly open.
- luxenotify `0.1.0-5`: GTK display initialization deferred until the first card. Empty daemon probe PSS ~2 MiB; 3/3 tests pass, including bus/count registration without a display and nonfatal display failure.
- luxepolkit `0.1.0+gtk4-5+luxe14`: headless single-instance GApplication registration, GTK only for auth/error windows, current theme on first prompt and live scheme updates afterward. 2/2 tests pass with mocked authority registration and real first-prompt focus/grab/cancellation behavior. No live agent replaced.
- luxup `0.1.1-1+luxe9`: hidden window/list creation deferred until opened; resident application hold and safe pre-window result updates. 2/2 tests pass. Probe saving is only ~0.3 MiB; tray/GTK startup still needs profiling.
- pcmanager `1.4.0+gtk4-4+luxe21`: browser/libfm setup deferred until requested; real first-browser IPC test passes. Correct compiled About version to match package (previously luxe18 in luxe20). No reliable RAM reduction measured from deferral alone. Five standard non-UI tests pass; opt-in drag/drop test is skipped, not counted as UI acceptance.
- frame `0.1.0-1+luxe10`: native WebP scaled decoding keeps image features/bounds while reducing a 32-megapixel diagnostic fixture's peak RSS ~165 → 40 MiB and decode time ~370 → 170 ms. Existing viewer/WebP test passes; no full-suite or hardware idle reduction is claimed. Candidates not installed; packaging status tracked in `light.md`.

## 2 October 2026 — resource optimization measurement pass

- Jerry approved `light.md`, covering every package in RAM → CPU → disk order with theming first. Added an isolated diagnostic runner for theme/bridge/icon/default-renderer/Cairo memory comparisons. This is test tooling, with no shipped package change or rebuild.
- The main machine's installed expanded icon package is `2026.09.08-1`. In fresh isolated LuxeTerm processes, its Luxe-Dark icons add approximately 31 MiB PSS over Adwaita icons; the current compact `2026.09.09-5` artifact selected from private staging adds approximately 9–10 MiB. This roughly 22 MiB reduction was observed with both renderer choices over two repeats. The Chromebook package cohort and whole-system memory regression are still being investigated. No packages installed and no user themes/configuration changed.

## luxurystore 0.1.0-17 — system state directory on minimal installs

- 2 October 2026: A fresh Debian-minimal Chromebook could open LuxuryStore but its unprivileged UI failed with `could not create state directory /var/lib/synaptic - mkdir (13: Permission denied)`. The DEB had installed an empty directory under `/usr/var/lib/synaptic`, because `CMAKE_INSTALL_LOCALSTATEDIR` was relative to the `/usr` prefix. Install the directory explicitly at `/var/lib/synaptic` (root-owned, mode 0755); the existing backend can read it and no privileged operation moves into the UI. A new staged-install regression checks the real install rules and rejects `/usr/var/lib/synaptic`.
- Fresh dual-ABI Release build and all 8 CTests pass, including APT 6/7 backend, ABI, Flatpak-default and the new state-directory test. The DEB passes payload audit; its only state path is `./var/lib/synaptic/` at mode 0755, and the staged executables have no RPATH/RUNPATH. Promoted `0.1.0-17` (SHA-256 `c1c2ef347c2bcf74ee4ca39c26c00d865c5720a738f46681b3d33b820701b3c6`) with `0.1.0-16` as sole prior; `0.1.0-15` was moved to recoverable desktop trash. RPM was attempted but `rpmbuild` is unavailable. Nothing installed on the development machine; Chromebook retest is pending.

## luxeappearance 0.3.0+gtk4-2+luxe18 — desktop schema on minimal installs

- 2 October 2026: A Debian-minimal Chromebook lacked `org.gnome.desktop.interface`. Luxe Appearance publishes GTK theme, icon, cursor, font and color-scheme choices there, but neither GTK nor libadwaita guarantees the schema package. Make `gsettings-desktop-schemas` a hard DEB/RPM dependency; its Debian package pulls a GSettings backend. No UI/theme logic changed. Chromebook theme retest is pending.
- Fresh Release build and all 3 Appearance CTests pass. The DEB passes payload, version, dependency and no-RPATH checks; `Depends` contains `gsettings-desktop-schemas`. Promoted `luxe18` (SHA-256 `49f89822f18710c32980aafcceb7f4f5a9c54f04e3e88cd64095c0b98ef2f446`) with `luxe17` as the sole previous version; `luxe16` was moved to recoverable desktop trash. RPM generation was attempted but this host lacks `rpmbuild`. Nothing installed on the development machine.

## luxedm 1.4.1-1+luxe11 — input driver on minimal installations

- 2 October 2026: A Debian-minimal Chromebook showed the greeter but accepted no keyboard or touchpad input. Its Xorg log repeatedly said “No input driver specified, ignoring this device,” and `xserver-xorg-input-libinput` was absent. `xserver-xorg-core` can start a display without an input driver, so LuxeDM now depends explicitly on `xserver-xorg-input-libinput` in DEBs and `xorg-x11-drv-libinput` in RPMs. No greeter code or input-grab behavior changed. The first Xorg attempt also logged “no screens found” before the service restart; that is a separate display-start timing issue, not the cause of the input failure on the running second attempt. Installing `xserver-xorg-input-libinput` over SSH and restarting LuxeDM restored the Chromebook greeter.
- Fresh Release build and all 9 LuxeDM CTests pass. The DEB passes payload audit, has no RPATH/RUNPATH, and its control file contains the new hard dependency. Promoted `luxe11` (SHA-256 `c2cba14083e0e5b88d21331df9f3bfafde4f4ba89906577ed8a23dd043d05f8c`) with `luxe10` as the sole previous copy; `luxe9` was moved to recoverable desktop trash. RPM generation was attempted but this host lacks `rpmbuild`. Nothing was installed on the development machine.

- `luxe-default.png` (1 MB) is no longer shipped by `pcmanager`. Nothing defaults to it any more: fresh profiles use the generated luxesession wallpaper and Appearance hands off the palette's own SVG, so it was 1 MB of orphaned payload. This is a pre-alpha break, so no migration rewrites existing profiles that still name the old path. `luxesession/data/palettes/` is now the single source of every background the suite installs.

## Coherent Tokyo Night default — luxesession luxe33 / luxepanel luxe37 / pcmanager luxe19 / luxeappearance luxe17

- 2 October 2026: A fresh install landed on none of the six palettes. The shipped defaults named stock `Adwaita-dark`, the legacy `Syscrash-Luxe` Openbox theme and `luxe-default.png`, so a new machine got Luxe icons wrapped around otherwise stock components, and the orbit artwork was never shown. Fresh profiles now seed `Luxe-tokyo-night` for GTK and the matching Openbox variant, one desktop, and the generated `tokyo-night.svg` wallpaper. The session's XSettings fallback default and `data/LXDE/desktop.conf` were repointed to match. Appearance already preferred the palettes and only ever used `Adwaita` as a fallback when the bridges were missing, so no selection logic changed.
- Retired the `Afterpiece`, `Syscrash-Light`, `Syscrash-Luxe` and `Syscrash-Luxe-Light` theme directories. They were reachable only as manual choices; the six generated palettes carry the branding and each ships its own Openbox variant. The 6x6 control masks those themes provided are now `data/openbox-masks/` and are copied into every palette, so the window buttons are unaffected. `luxeappearance`'s Openbox fallback moved from the retired names to `Luxe-tokyo-night`/`Luxe-light`, which also removes the last dangling reference to them. Upgraded profiles whose `rc.xml` or `lxde-rc.xml` still names a retired theme are rewritten to the new default, following the existing `lxterminal` to `luxeterm` precedent; any other theme the user chose is left alone. `luxe-default.png` is still shipped so existing profiles that reference it keep a working wallpaper.
- The shipped default panel was replaced with the maintained profile. It had `background=1` with a baked `background.png`, which painted over the palette-aware panel background the panel otherwise derives at runtime; it now uses `background=0`. It also hardcoded `fontcolor=#ffffff`, which ignores the theme foreground and reads poorly against palettes such as Tokyo Night where the foreground is a pale blue; it now inherits via `usefontcolor=0`. Panel height moves from 26 to 28 to match the panel thickness the inset tray artwork is sized against. The default no longer pins a `launchbar` to `firefox.desktop`, and drops the `cpu` plugin and two duplicate back-to-back `space` spacers. Clock is `ClockFmt=%R`, 24-hour, matching LuxePanel's own `DEFAULT_CLOCK_FORMAT`.
- `data/default/panels/panel` is the file the package installs; `panel.in` is referenced only by the test. Both are now generated from the same source and drift-checked, because editing `panel.in` alone had no effect on the shipped payload. `tests/default-panels.py` rejected the new profile because its known-plugin set was missing `separator`, a core in-binary plugin compiled at `CMakeLists.txt:42`; that is fixed. `test-openbox-compact.py` no longer copies the retired themes and uses the `Before` fixture, which already carries the old taller padding, for its compact-versus-tall comparison.
- Fresh `Unix Makefiles` Release builds are clean and all tests pass: luxesession 13/13, luxepanel 13/13, pcmanager 6/6, luxeappearance 3/3. DEBs audited for payload and RPATH, and rotated into `artifacts/` with checksums and manifest regenerated. Nothing installed and no live desktop altered.

## luxesession 0.5.5+gtk4-4+luxe32 — deeper Luxe Light surfaces and wallpaper

- 2 October 2026: Deepened the Luxe Light palette, whose near-white greys read as glaring on a light desktop. Surfaces `bg`/`view`/`raised` and the mid-tones `accent`/`standalone` scale by 0.77 in gamma space; `border` by 0.738 and `muted` by 0.693, because `bg` falls toward them and equal scaling regressed both. At a flat 0.77 `muted` landed at 4.00:1 on `bg`, under the WCAG AA floor, and `border`'s separation slipped from 2.07:1 to 1.94:1. The shipped values hold `muted` at 4.52:1 and `border` at 2.08:1. `fg` and `on_accent` carry text and are never scaled. Every pair the stylesheets actually form stays at or above AA: body text 6.16:1 on `bg`, 6.76:1 on `view`, 5.66:1 on `raised`; white-on-accent 7.88:1; white-on-`standalone` 10.67:1; `standalone` as an accent foreground 4.55:1 on `bg`. Accent is only ever a background in the bridges (`row:selected`, `menuitem:hover`, `theme_selected_bg_color`), never a foreground, so the white-on-accent pair is the one that governs it.
- The wallpaper previously reused the interface colors verbatim, so it could not sit at a different depth. Each preset now derives its own wallpaper tones from its interface tones through a new `luxe_scale_hex()` helper in `BuildThemes.cmake`, taking a factor in thousandths with round-half-up. The per-preset factor defaults to `1000`, leaving the five dark presets byte-identical to their previous output; Luxe Light sets `600`, putting its wallpaper a further 40% below the interface. The wordmark uses a lighter factor so it stays a quiet brand mark. `wallpaper.svg.in` now consumes `@wp_bg@`, `@wp_view@`, `@wp_raised@`, `@wp_accent@` and `@wp_fg@` and references no interface color.
- CMake's `math(EXPR)` accepts neither float literals nor bare variable references, so the helper scales in fixed point (`(${channel} * ${factor} + 500) / 1000`) and converts through an explicit hex digit table. Verified against 18 cases covering every shipped value, both identity cases, white and black, and an unclamped over-unity factor.
- Fresh `Unix Makefiles` Release build is clean and all 13 luxesession CTests pass, including `luxesession-theme-watch`, `luxesession-native-headers`, `luxesession-real-header` and `luxesession-openbox-compact`, which exercise the regenerated themes. Regenerating the catalog changes `light.svg` only; the five dark wallpapers match the installed payloads exactly. RPM release advances to `5.luxe32`. Nothing installed and no live desktop altered.
- Note for future maintenance: the preset branches are chosen with `STREQUAL`, so `BuildThemes.cmake` depends on CMP0054 being `NEW`. Every consuming `CMakeLists.txt` supplies that through `cmake_minimum_required(VERSION 3.25)`, but driving the file from `cmake -P` without it makes `id STREQUAL "dark"` dereference the `dark` variable, so the Luxe Dark preset silently falls through to the Tokyo Night branch.

## luxup 0.1.1-1+luxe8 — opaque tray tile under a compositor, native icon size

- 30 September 2026: Jerry's screenshot showed a black LuxUp tile with an oversized check mark once picom ran. The luxe6 rule kept the tile transparent whenever a compositor owned `_NET_WM_CM_S0`, but compositors blend only top-level windows; a transparent XEmbed child inside the panel's slot comes out black. LuxUp now always paints the panel background and inset outline. The glyph is drawn at the icon theme's grid size that fits a 1 px inset (22 px on a 28 px panel) instead of stretching Luxe-Dark's fixed 22/24 px symbolic drawing to 26 px; the XEmbed window and click target are unchanged.
- Reproduced in a private Xephyr/Openbox/picom session with LuxePanel: installed luxe7 shows the black tile, luxe8 an opaque tile with the smaller glyph. The behaviour test now asserts an opaque painted tile with a compositor present and the grid-sized, centred glyph at four tile sizes. Release 2/2 tests, DEB audit and no-RPATH pass; SHA-256 `03655e67ee278ff4f3b3c015c3801371d39e4b07f2ab515223a8d703d11474a8`. Nothing installed.

## Ten-fix release — 14 packages

- 30 September 2026: **luxeappearance-obconf luxe8** fixes a crash in the Openbox theme list: an auto-freed path was declared without `NULL`, so a symlinked or control-character theme folder made the scan free a garbage pointer. The tree test now adds such folders; with GCC's pattern-initialised locals the old code fails under ASan at `theme.c:370`, the fix passes.
- **pcmanager luxe18**: the single-instance socket name compared against the two-character constant `'\\0'`, so every DISPLAY host (even the empty host of `:0`) was replaced by "display". A new behavioural test compiles the real function (the old grep test could not see this). After upgrading, log out and in so old and new processes share one socket name. Properties now formats 64-bit file and folder counts correctly. Unused copy/transfer wrappers and a monitor helper are removed.
- **luxesession luxe31**: `luxesession-db` without `--mode` now uses its intended "display" mode instead of four GLib criticals and a permanent hang; an unknown mode exits 2 with a message (Vala and maintained C). An unused clipboard variable is removed.
- **matinee luxe8**: the paned playback area declares its real minimum (240 px surface + 80 px playlist), ending the startup "Trying to measure GtkOverlay for height of 94" warning; the window's minimum height grows accordingly.
- **luxehotkey luxe9**: errors shown inside the app now have the window as parent; a startup error waits until dismissed instead of vanishing as the process exits; nested X servers that create only `/tmp/.X11-unix/X<n>` count as local. The round-trip-only serializer is marked test-only.
- **luxepanel luxe36**: 19 unused variables, three unused functions, the GTK2 launchtaskbar RC string and about 190 lines of commented-out GTK2 code (CPU-governor menu, task state handler) removed; strict `-Wall -Wextra` now reports no unused code in authored panel sources. Shared dialog helpers are marked `G_GNUC_UNUSED` (no code change for other consumers).
- English-only desktop and menu metadata: 5,082 inline `Key[locale]=` lines removed from 32 files in luxemenu-data luxe8, luxerandr luxe10, luxeinput luxe7, luxepolkit luxe13, luxesession, pcmanager, luxeappearance luxe16, luxehotkey, luxetask luxe8 and luxeterm luxe11. **shotty luxe9** is listed once, under Accessories.
- All 14 Release builds pass their tests (Session 13/13, Panel 13/13, PCManager 6/6 and the rest), DEB audits, no-RPATH checks and desktop validation (the Xsession `DesktopNames` exception remains). Nothing installed or restarted.

## luxesession luxe30 / luxepanel luxe35 / pcmanager luxe17 / luxehotkey luxe8 — Session Settings port, effective WM key, libfm modules noise

- 30 September 2026: LuxeSession Settings (`luxesession-edit`, "Startup Applications" in Luxe Settings) is ported from a bare `GtkDialog` with its own main loop to `AdwApplication`/`AdwApplicationWindow`: header bar with Cancel, Save (`suggested-action`, Ctrl+S) and an About menu, Startup/Advanced pages in an `AdwViewStack` switcher, destructive Remove, and a single window across repeat launches. Autostart toggles, add and remove still save immediately; Save writes the window manager and closes.
- The Advanced window-manager field previously edited `windows_manager/command`, which LuxeSession ignores whenever the legacy `window_manager` key exists (every packaged and migrated profile), so it had no effect; it could also offer the retired `openbox-lxde`. It now reads and writes the key LuxeSession obeys and offers `openbox`.
- Turned off GtkTreeView type-ahead in the autostart list: any key reaching the focused list (Ctrl+S, Alt+F4) raised a GTK4 `gtk_css_node_insert_after` critical from the deprecated view's search entry (traced with Debian debuginfod symbols to `gtktreeview.c:10017`).
- The private libfm no longer warns "modules directory is not accessible" when an application registers no module directory of its own (PCManager); an unreadable Panel/Hotkey plugin directory still warns. Plugin loading is unchanged.
- New `luxesession-session-edit` nested-X test: clean startup (no criticals/warnings), single instance, Ctrl+S saves the effective key for legacy and modern profiles and replaces `openbox-lxde`. Release builds pass Session 13/13, Panel 13/13, PCManager 5/5, Hotkey 4/4; DEB audits, no-RPATH and desktop validation pass; `libadwaita-1-0` arrives through shlibdeps (RPM requires updated). Nothing installed or restarted.

## luxesession luxe29 / luxepanel luxe34 — compositor start, taskbar labels, stale icons

- 30 September 2026: picom 12 exits at login unless a render backend is chosen, and neither Luxe nor Debian installed a config, so the session ran uncomposited. LuxeSession now ships `defaults/picom/picom.conf` (`backend = "xrender"; vsync = true;`); migration adds it only where the user has none. Verified in nested X: without it picom exits 1; with it picom owns `_NET_WM_CM_S0`. An existing user config is left untouched.
- LuxePanel task buttons drop Adwaita's 5 px vertical padding (their border still frames them). At a 28 px panel the label was allotted 18 px while needing 21, logging a GTK measure warning for every new window. Labels now fit at 24 px and taller. Rendering is pixel-identical at 28 px; the change removes an overflowing layout and its log noise. `taskbar-click` now runs at 28 px and fails on luxe32/33, passes on luxe34.
- When an application frees its legacy icon pixmap mid-read, the taskbar now discards the unreadable pixels and uses the fallback icon, logging at debug level instead of warning.
- Session migration regression now covers the luxe28 `libfm.conf` terminal rewrite (plain value rewritten, custom and argument forms kept, symlink refused). Release builds pass Session 12/12 and Panel 13/13, DEB audits and no-RPATH checks. Nothing installed or restarted.

## luxesession luxe28 / luxepanel luxe33 / pcmanager luxe16 / luxehotkey luxe7 — dead session paths, known terminals

- 30 September 2026: LuxeSession's logout dialog drops its Switch User path. It only knew GDM's retired `gdmflexiserver`, KDM, LXDM and LightDM, none of which is LuxeDM, so the button could never appear; logging out returns to LuxeDM to change users. The never-reached "Lubuntu" session defaults block (scrot, leafpad, sylpheed and similar) is removed from both the Vala source and its maintained C.
- The private libfm built into Panel, PCManager and Hotkey now knows LuxeTerm and its `lxterminal` alias without the uninstalled upstream `terminals.list`, ending the "terminal isn't known" warning at every login. Session migration rewrites an exact `terminal=lxterminal` in `~/.config/libfm/libfm.conf` to `luxeterm` and leaves any other choice alone.
- `luxesession-real-header` pins its private LuxeTerm to fixed colours: its row probe looked for a near-black terminal background, which palette-follow (LuxeTerm luxe9) had replaced with the Tokyo Night background. It now passes, measuring 51 -> 31 px.
- Release builds pass all tests (Session 12/12, Panel 13/13, PCManager 5/5, Hotkey 4/4), DEB payload audits and no-RPATH checks. Migration checked on private profiles (plain `lxterminal` rewritten; `xterm -e %s` untouched); terminal warning checked in nested X for luxeterm, lxterminal and a negative urxvt control. Nothing installed or restarted.

## Cruft cleanup release — 12 packages

- 30 September 2026: **luxepanel 0.10.1+gtk4-8+luxe32** removes the dead GTK2/GTK3 implementation (about 8,800 of 42,400 source lines: every `#ifndef ENABLE_GTK4`, `#else` arm, `#if 0` and always-true `GTK_CHECK_VERSION`), migrates its fixed-display tests to the shared nested-X runner, and drops orphan test scripts. Preprocessed output of all 88 translation units is identical before and after apart from `__LINE__` values.
- The same release ships without translation catalogs for luxeterm luxe10, pcmanager luxe15, luxeinput luxe6, luxemenu-data luxe7, luxepolkit luxe12, luxesession luxe27, luxeappearance-obconf luxe7, luxetask luxe7, luxeappearance luxe15, luxerandr luxe9 and luxehotkey luxe6. Releases are English-only for now; 801 `.po` catalogs were removed and every translator credit is preserved in each package's `TRANSLATOR-CREDITS.txt`, installed under `/usr/share/doc`. PCManager's committed `.gmo` files, autotools and intltool leftovers are gone.
- Not version-bumped because compiled output is byte-identical: hotkey/panel/polkit/pcmanager/randr/task/luxup now take the shared byte-identical `luxe-spacing.h` and `luxe-dialogs-gtk.h` from `shared/` instead of per-package copies; `internal/libfm` and `menu-cache` shed 109 unused files (88,710 to 34,522 code lines).
- Release and independent test runs pass for every package except `luxesession-real-header`, which also fails on the untouched pre-change snapshot (it drives the installed luxeterm). Fresh Release builds, DEB payload audits, no-RPATH, no-`/usr/local` and desktop validation pass; the Xsession `DesktopNames` extension remains the known validator exception. Nothing installed or restarted; RPM still unverified.

## lucid 0.1.0-1+luxe9 — follow desktop palette

- 30 September 2026: Follow the selected luxe Light, Dark, Nord, Mocha, Dracula or Tokyo Night theme in the editor and embedded terminal. Add matching neutral Light/Dark syntax schemes; keep the saved custom scheme available when the new default-on preference is switched off. Existing windows update on GTK theme changes. Shorten the unsaved-file alert heading to avoid a GTK layout warning while retaining the document name in its body.
- Release and independent ASan/UBSan pass 2/2 CTests. Package schema/styles, dependencies, version, ownership and no-RPATH audit pass. Promote `luxe9`, retain `luxe8` as sole prior, and move `luxe6` to recoverable trash. Nothing installed or restarted; RPM remains unverified.

## luxeterm 0.3.2+gtk4-2+luxe9 — follow desktop palette

- Derive VTE foreground, background and ANSI16 colors from the active GTK theme by default, updating open windows when it changes. A preference switch restores the existing saved preset/custom colors without overwriting them. Clamp wide-gamut GTK semantic colors for valid VTE RGB.
- Release and independent ASan/UBSan pass 2/2 CTests, including six actual theme palettes, persistence, opt-out and command forwarding. Package dependencies, version, ownership and no-RPATH audit pass. Promote `luxe9`, retain `luxe8` as sole prior, and move `luxe6` to recoverable trash. Nothing installed or restarted; RPM remains unverified.

## luxepanel 0.10.1+gtk4-8+luxe31 — unobstructed system-tray outline

- 29 September 2026: Keep ordinary XEmbed system-tray frames and foreign icons inside the panel's 1 px outline only where they touch it. Keep already-inset slots, custom-background panels, panel geometry, icon spacing and client behavior unchanged. An edge-aware client may retain its full tile when it draws its own line.
- Private-X geometry and edge-pixel regression passes across all six palettes and fails against luxe30; inherited and opaque foreign clients plus a marked full-size client are covered. ASan/UBSan passes 13/13 CTests. All 13 Release cases pass across two full runs and isolated reruns of flaky unmodified taskbar/volume click fixtures; neither tray test failed. Payload/dependency/version/no-RPATH audits pass. Promote luxe31 with luxe30 as sole prior, pruning luxe29. RPM blocked by missing rpmbuild; nothing installed or restarted.

## luxup 0.1.1-1+luxe7 — coordinate edge ownership with Panel

- Publish an XEmbed edge-aware marker whenever LuxUp creates its tray window, so Panel luxe31 leaves its already outline-painted tile full-size instead of creating a double stripe. The luxe6 theme-color fallback, compositor transparency, icon size and update behavior remain unchanged.
- Release and ASan/UBSan pass 2/2 CTests, including marker/property, real X11 pixels, theme switches and tray recreation. Payload/dependency/desktop/version/no-RPATH audits pass. Promote luxe7 with luxe6 as sole prior, pruning luxe5. RPM blocked by missing rpmbuild; nothing installed or restarted.

## luxup 0.1.1-1+luxe6 — continuous panel outline through tray

- 29 September 2026: In non-composited X11 sessions, paint LuxUp's XEmbed tile with the selected panel background and continue the 1 px inset outline across its top and bottom. Preserve transparent ARGB when a compositor is active, the inset icon artwork, tile/click size and update/authentication behavior. Repaint on GTK theme changes.
- Fresh Release and ASan/UBSan pass 2/2 CTests, including private-X pixel checks for the non-composited seam, compositor transparency and dark/light theme switches. Payload/dependency/desktop/version/no-RPATH audits pass. Promote luxe6 with luxe5 as sole prior, pruning luxe4. RPM blocked by missing rpmbuild; nothing installed or restarted.

## luxepanel 0.10.1+gtk4-8+luxe30 — inset automatic panel icons

- 29 September 2026: Keep automatic icon tiles at the configured size capped by panel thickness; render artwork 2 px smaller with 1 px margins. Replace volume's forced 32 px image with shared automatic sizing, and notify size-dependent icons on panel height edits. Preserve explicit image dimensions, foreign tray clients, panel layout and existing interaction routing.
- Release and independent ASan/UBSan pass all 13 CTests, with targeted reruns after rendering refinement. Probe 12 size combinations, explicit-size preservation and real GtkImage snapshot rims; existing outline/menu/tray/taskbar/keyboard and actual volume click/popup cases pass. Payload/dependency/version/no-RPATH audits pass. Promote luxe30 with luxe29 as sole prior; prune luxe28. RPM blocked by missing rpmbuild; nothing installed or restarted.

## luxup 0.1.1-1+luxe5 — inset tray icon artwork

- Center artwork 2 px smaller than the tray's shorter dimension, leaving 1 px on each side without resizing the XEmbed/click window. Use the same path in all five states; update/authentication behavior is unchanged. Centralize the release string for compiled and package/test versions.
- Fresh Release and independent ASan/UBSan pass 2/2 CTests, including 20 full-bleed icon render cases and existing backend/authentication/tray/UI behavior. Payload/dependency/desktop/version/no-RPATH audits pass. Promote luxe5 with luxe4 as sole prior; prune luxe3. RPM blocked by missing rpmbuild. No installation or live package-manager/polkit changes.

## luxepanel 0.10.1+gtk4-8+luxe29 — palette-aware panel outline

- 29 September 2026: Separate system-themed panels from matching wallpaper with a subtle 1 px inset outline, using the active foreground at 22% opacity. High contrast uses the full foreground. No padding, panel height/geometry or strut changes; preserve custom image/transparent backgrounds and existing menu/input/tray behavior.
- Fresh Release and independent ASan/UBSan pass all 12 CTests, with targeted reruns after correcting a custom-image fixture. Actual pixel/allocation checks cover six palettes, all docking edges, high contrast and custom-background opt-outs; the outline assertion fails on old luxe28. Preview inspected; payload/dependency/compiled-version/no-RPATH audits pass. Promote luxe29 with luxe28 as sole prior, pruning luxe27. RPM blocked by missing rpmbuild. Nothing installed or restarted. An unrelated existing custom-image red/blue packing defect is recorded in astra.md for separate work.

## luxesession 0.5.5+gtk4-4+luxe26 — compact GTK/libadwaita headers

- 29 September 2026: The reported Luxeterm bar is client-drawn, not Openbox. Share compact header padding/control/image minimums across all six full GTK themes and their managed libadwaita bridges. Preserve fonts, palette colors, ordinary body/popover buttons and natural expansion for large titles/subtitles or rich controls. Existing custom-CSS preservation and external/high-contrast suspension remain unchanged; no replacement libadwaita stylesheet or new runtime process.
- Fresh Release and independent ASan/UBSan pass 12/12 CTests each, including 18 native GTK3/GTK4/Adw palette cases and real Luxeterm pointer controls/menu/input in a private desktop. Real terminal first-content-row measurement drops 51 -> 31 px (~61%); native test headers measure 34/28/26 px respectively at Sans 10. Preview inspected; DEB payload/version/dependency/application desktop/no-RPATH audits pass. Promote luxe26 with luxe25 as sole prior; prune luxe24. RPM attempt blocked by missing rpmbuild. Appearance/Luxeterm payloads unchanged; nothing installed or restarted.

## luxesession 0.5.5+gtk4-4+luxe25 — compact Openbox title bars and buttons

- 29 September 2026: Reduce shared Openbox padding from 8/5 to 5/0 px horizontal/vertical and redraw close, maximize, restore and minimize masks from 10x10 to 6x6. Apply consistently to all six palette variants and the original Syscrash-Luxe light/dark pair. Preserve title fonts, user configuration and palette colors; GTK headerbars and LuxePanel menus are unchanged. Openbox-native popup margins also tighten because padding is shared.
- Real frame measurements at font size 10 drop from 24 to 14 px, about 58% of the prior height; larger/accessibility fonts retain their metrics. Fresh Release and independent ASan/UBSan pass 10/10 CTests each, including 24 actual theme/font combinations and pointer maximize/restore/minimize plus close delivery in a private unmapped-host Xephyr/Openbox desktop. Preview inspected; DEB payload/dependency/application desktop/version-definition/no-RPATH audits pass. RPM attempt blocked by missing rpmbuild. Promote luxe25 with luxe24 as sole prior; remove luxe23 from old. Nothing installed or restarted.

## luxepanel 0.10.1+gtk4-8+luxe28 — menu hover and palette-correct tray frames

- 29 September 2026: Give application rows and category headers a palette-colored hover tint and inset accent edge with brief GTK transitions. Widget-owned motion controllers leave selection, search/input grabs and category activation unchanged; separators remain inert and GTK animation settings remain respected.
- Resolve ParentRelative XEmbed frame backgrounds from the selected theme's named background color. Fixed-mode Tokyo Night/Catppuccin and other dark presets no longer receive near-white frames just because prefer-dark is false. Preserve backgrounds deliberately painted by opaque foreign clients.
- Fresh Release and independent ASan/UBSan pass 12/12 CTests: actual hover/leave pixels and keyboard search in all six presets, inherited versus opaque tray pixel checks, real GTK3 embedding/unmap/hide/show, and existing keyboard grabs/category state/taskbar/control/volume coverage. Inspect hover previews; DEB payload/dependency/compiled-version/no-RPATH audits pass. New style-context color lookup retains a GTK deprecation for older supported versions. Final suites run serially after a fixed-display harness collision. Promote luxe28 with luxe27 as sole prior; prune luxe26 from old. RPM attempt blocked by missing rpmbuild; nothing installed or restarted.

## luxesession 0.5.5+gtk4-4+luxe24 — coordinated orbit wallpapers

- Redraw Jerry Howell's supplied two-ring/eight-ray emblem as crisp shared vector geometry on all six wallpapers, with restrained palette-colored glow and a small lowercase luxe wordmark. Refresh neutral Light/Dark with subdued translucent folds; retain the four colored landscape silhouettes. Leave the reference WEBP unchanged. No raster bundles, font assets, downloads or new runtime process.
- Fresh Release and independent ASan/UBSan rebuild pass 9/9 CTests. Existing Appearance native-widget test passes 18 GTK3/GTK4/libadwaita cases and decodes all six SVGs; all wallpaper previews inspected. DEB payload/dependency/application desktop/version-definition/no-RPATH audits pass. Promote luxe24 with luxe23 as sole prior; prune luxe19 from old. RPM attempt blocked by missing rpmbuild. Appearance payload is unchanged and stays luxe14; no packages installed, configuration changed or session restarted.

## luxeappearance 0.3.0+gtk4-2+luxe14 — six global palette presets

- 29 September 2026: Add luxe Light, luxe Dark, Nord, Catppuccin Mocha, Dracula and Tokyo Night swatches. Apply sets global GTK/color-scheme, matching Openbox and existing icon modes; Light selects softened neutral light, Dark restores the last selected dark preset. Optional static matching wallpaper uses PCManager via argv. Lucid editor/syntax and Luxeterm/VTE palettes remain untouched.
- Preserve custom GTK4 CSS behind a marked color import, refuse unowned/symlinked or unsafe bridge files, and extend existing Apply rollback to seven files. A durable bounded journal offers explicit Keep current/Restore previous recovery after interruption; validate paths, snapshots and GSettings enum ranges before restoring. Preserve existing manual themes and unrelated configuration. Explain restart/sandbox limits in the UI.
- Fresh Release and ASan/UBSan pass 3/3 CTests, including 18 real GTK3/GTK4/libadwaita widget cases, all-six actual Apply, CSS preservation, remembered mode, Cancel, late failure rollback, damaged recovery values, symlinks, external themes and private wallpaper argv. Chooser/native previews inspected; payload/application desktop/dependency/version-definition/no-RPATH audits pass. Candidate DEBs luxe12–13 were not promoted; advance again for the final coordinated dependency on Session luxe22. RPM attempt blocked by missing rpmbuild. Promote luxe14 with luxe11 as sole prior, removing luxe10 from old. Nothing installed or restarted.

## luxesession 0.5.5+gtk4-4+luxe23 — full themes, wallpapers and managed bridge watch

- Own the single six-palette catalog and generate complete GTK3/GTK4 Adwaita-derived stylesheets, matching flat Openbox variants, semantic legacy/modern libadwaita bridges and six original SVG landscapes. Import the complete version-matched GTK resource base rather than copying installed assets or vendoring another theme implementation. Keep legacy themes/defaults available; no forced user migration, downloads or new daemon.
- Run a coalesced event-driven color-bridge watch inside Session. High-contrast/external-theme changes suspend only owned overrides; restored presets regenerate compatible colors. Preserve custom CSS and safely handle first state creation, malformed/unknown state, final symlinks and teardown. Coordinate maintained C/Vala. GTK4 >=4.16 receives custom properties; older GTK4/GTK3 avoid unsupported syntax. Refine popup/arrow and entry-focus specificity after visual QA, eliminating stock white popups/blue focus in colored themes.
- Fresh Release and ASan/UBSan pass 9/9 CTests. The private MIME D-Bus test now copies the newly built ELF into its fixture so masking the real HOME does not hide the test executable. All six wallpapers decode; native theme/bridge checks and DEB payload/dependency/mode/version-definition/no-RPATH audits pass. Application desktop files validate; the existing Xsession DesktopNames extension is rejected by desktop-file-validate but retained for display-manager integration. Candidate DEBs luxe20–22 superseded without promotion; final luxe23 retains luxe19 as sole prior, removing luxe18 from old. RPM attempt blocked by missing rpmbuild. Ubuntu/Fedora native renderer/runtime validation remains outstanding; no package installed.

## luxepanel 0.10.1+gtk4-8+luxe27 — theme-aware CSS menu wordmark

- Render the stock lowercase luxe button as native text with a restrained CSS gradient, hover/open treatment and accessible action. Keep existing non-focusable pointer routing and bounded keyboard-grab recovery. Auto converts only known stock assets; explicit image mode and arbitrary custom images remain intact.
- Under a selected Luxe palette, replace only the exact stock bitmap panel background at runtime with system colors, without rewriting the profile. Its stock white clock/text override inherits palette text; preserve other custom backgrounds/colors. Explicit paired stock images also respect fixed-mode presets. High-contrast themes remove the wordmark gradient.
- Release and independent ASan/UBSan pass 11/11 CTests in serial suites. Extended actual-input regression exercises the stock wordmark through pointer click, search/Enter, held Super, competing/expired grabs and Escape. Additional light/Nord menu previews pass and are visually inspected. Concurrent full suites collided in older fixed-display fixtures; serial reruns pass. Payload/application desktop/dependency/version-definition/no-RPATH audits pass. Promote luxe27 with luxe26 as sole prior, removing luxe25 from old; RPM blocked by missing rpmbuild. Nothing installed or restarted.

## luxesession 0.5.5+gtk4-4+luxe19 — synchronize browser URL defaults

- 29 September 2026: Fix browser selection updating only the session command while web links follow stale higher-priority MIME defaults. Apply HTTP/HTTPS associations centrally through SessionSet, including manual commands; reject invalid commands/failed writes without accepting the setting. Reuse matching installed URI launchers or preserve custom arguments in hidden command-hashed launchers. Disable and login leave independent defaults alone.
- Replace the duplicated legacy MIME writer with one bounded atomic helper using the active XDG config location. Preserve unrelated file/mail/media/custom-protocol defaults, comments and association lists. Skip the browser's broad optional MIME takeover; retain that confirmation for other applications. Coordinate maintained C/Vala and compiled/DEB/RPM versions.
- The real D-Bus regression fails with installed luxe18 and passes with luxe19. Eight Release and eight ASan/UBSan CTests pass; both new behavior tests pass five repeated sanitizer runs (toolkit leak detection disabled). Effective GIO/xdg defaults, real dummy URI delivery, desktop-specific precedence, invalid commands, symlinks, malformed config and preservation are covered. DEB payload, desktop, dependency, compiled-version and no-RPATH checks pass. RPM generation remains blocked by missing rpmbuild.
- Separately, with explicit authorization, back up the user's MIME config and repair only HTTP/HTTPS defaults to microsoft-edge.desktop; xdg-settings, xdg-mime and GIO confirm Edge. No package installed, browser launched or running session restarted. Stage luxe19 with luxe18 as sole prior, removing luxe17 from old.

## matinee 0.1.0-1+luxe7 — silent-video pacing and seek after completion

- 28 September 2026: Fix video-only playback comparing each frame's timestamp with itself, which decoded a one-second clip in about two milliseconds. Pace presentation against a pause-aware monotonic clock, deliver frames after waiting, and finish only after decoder drain and the last frame interval. Seeking after Ended no longer discards its target by restarting at zero. Audio's existing device clock and A/V EOF/drain ownership are unchanged.
- Ten behavior cases pass in fresh Release and three repeated ASan/UBSan runs (toolkit leak detection disabled), including silent Y4M/H.264 elapsed time, all 24 delayed-frame deliveries, pause/resume, seek, Stop, restart and existing MP3/private-ALSA coverage. Payload/desktop/no-RPATH audits pass. Promoted luxe7 with luxe6 as sole prior, removing luxe5 from old. RPM generation remains blocked by missing rpmbuild. Nothing installed; no live audio/session changes.

## luxepanel 0.10.1+gtk4-8+luxe26 — keyboard-grab recovery

- 28 September 2026: Reproduce missing menu input when a competing client holds its keyboard grab during popup opening. GTK entry focus alone cannot recover it, and GTK emits a close event after the failed grab. Check real GDK grab ownership and retry normal popup acquisition every 100 ms for at most two seconds, retaining rows during failure and cancelling recovery on success or teardown. Never steal another client's grab; expiry closes and clears the popup. Compiled and package versions now agree.
- Added private-X keyboard delivery tests for normal/held Super, search/Enter, temporary and expired competing grabs, Escape release and repeated opening. The targeted regression passes three times, and fresh Release/ASan builds pass all 11 CTests in serial runs. Two older fixed-display fixtures collided during concurrent suites; serial reruns resolve those test-only failures. Payload/version/no-RPATH checks pass. Promoted luxe26 with luxe25 as sole prior, removing luxe24 from old. RPM generation remains blocked by missing rpmbuild. No running panel was restarted or installed.

## luxurystore 0.1.0-16 — discover Flathub from installed Flatpak

- 27 September 2026: Select Flathub on launch when `/usr/bin/flatpak` is executable, matching LuxUp's existing availability gate. Load its catalogue through the normal asynchronous startup path after the window is ready. No separate persisted enable setting is introduced; manual catalogue filtering remains reversible, and adding a missing remote still requires explicit confirmation. Snap defaults and update authorization are unchanged.
- New actual-window regressions fail before the fix and pass afterward for both APT 6 and APT 7. Seven Release CTests pass, including absent-runtime behavior and fixture-backed installed/update rows; both new source-default cases pass three repeated ASan/UBSan runs. LuxUp's two existing CTests pass with additional missing-Flatpak coverage proving that no Flatpak command runs. LuxUp's shipped payload is unchanged, so no replacement LuxUp DEB is needed.
- All seven sanitizer tests also pass in split runs; the initial combined invocation exceeded the private-X harness's 90-second deadline. The unified Ubuntu/Debian DEB retains both APT backends, passes payload/desktop/loader checks, and carries no RPATH/RUNPATH. Promoted 0.1.0-16 with 0.1.0-15 as the sole previous DEB, removing 0.1.0-14 from old. Nothing installed and no live repositories changed. RPM generation remains blocked by missing rpmbuild.

## matinee 0.1.0-1+luxe6 — local filenames and MP3 regression

- 27 September 2026: Pass native playlist filenames directly to FFmpeg instead of escaped file URIs. The user's Battle.mp3 was valid, but spaces and the #1-10 directory became a different literal filename in FFmpeg's file protocol. Preserve Unicode, hash and literal percent sequences without changing decoding or sound configuration.
- Nine behavior cases pass in fresh Release and five repeated ASan/UBSan runs, including escaped-name video and real MP3 decode/resample/nonzero PCM output into an isolated ALSA null sink. The actual Battle.mp3 passes too; physical audibility and existing pacing/EOF-drain debt remain separate. Payload/desktop/no-RPATH checks pass. Promoted luxe6, retaining luxe5 as sole prior and removing luxe4 from old; nothing installed. RPM unavailable locally.

## shotty 0.1.0-1+luxe8 — responsive clipboard copying

- 27 September 2026: Stop disabling the entire window during clipboard persistence. Copy immediately, prevent duplicate OK submissions, keep Close/Back usable and cancel after five seconds. If the clipboard manager cannot retain the image, explain that Shotty must stay open to paste. Successful persistence still exits automatically; weak window ownership and request identity prevent late replies from retaining or quitting a closed/reused window.
- Six behavior cases pass in fresh Release and five repeated ASan/UBSan runs. A stalled private-X clipboard manager reproduces the original disabled-window defect; real private-clipboard paste, timeout, cancellation and close pass after repair. Separate simulated completion tests cover successful exit and late callbacks. Payload/desktop/no-RPATH checks pass. Promoted luxe8, retaining luxe7 as sole prior and removing luxe6 from old; nothing installed or copied to the live clipboard. RPM unavailable locally.

## luxup 0.1.1-1+luxe4 — reboot authorization race and manual refresh

- 27 September 2026: The user's reboot screenshot confirms pkexec ran before the graphical authentication agent registered. Retry only its exact C-locale “No authentication agent found” failure, with at most thirty cancellable half-second intervals on the worker. Never retry denied/dismissed authentication or a failed privileged refresh; preserve existing polkit policy and never store credentials.
- Check Again, the check menu action and tray middle-click now request a fresh authenticated APT-list refresh. Previously they read only cached lists, so they could not recover the failed login authorization. Explicit read-only `--check`/`--no-refresh` paths remain unchanged.
- The delayed-agent and actual-button regressions fail before the repair and pass afterward. Both Release CTests and five repeated ASan/UBSan runs pass, including exhaustion, cancellation, denial/dismissal, manual actions and tray/UI lifetime. Package tools/authentication are fixtures; nothing ran against live APT, Snap or polkit. Payload/desktop/no-RPATH checks pass. Promoted luxe4, retaining luxe3 as the sole prior DEB and removing luxe2 from `old`. Nothing installed; next-login verification remains with the user. RPM unavailable locally.

## matinee 0.1.0-1+luxe5 — playback lifetime fixes and psychedelic visuals

- 27 September 2026: Fix decoder notifications sometimes running GTK on a worker: use explicit UI-context sources and cancel queued video/completion delivery on Stop, replacement and disposal. Keep the engine alive through signal dispatch and pass full output-plane arrays to swscale.
- Fix a separately reproduced close-time crash caused by transferring the window's only playlist reference into GTK, then releasing it again. Balance model/factory references, stop and disconnect the engine before window teardown, remove the visual tick, dispose the template and track the application window weakly.
- At the user's request, replace the nearly uniform visual field with slowly morphing plasma, six-fold kaleidoscope and ripple/tunnel patterns. Audio drives shape/color; RGB trails and six-second crossfades provide variety without flashes or hard preset changes. Preserve fixed 320x240 rendering, the 30 Hz ceiling and audio-only presentation. Use relative monotonic animation time so silence and long system uptime do not stall motion.
- Eight behavior cases pass in Release and ten repeated ASan/UBSan runs. Actual H.264/AAC playback and window close pass in private Xephyr/Openbox with an ALSA null sink. Visual previews inspected; payload/desktop/loader checks pass. Promoted luxe5 with luxe4 as the sole prior DEB; nothing installed. Original-file/physical-audio retesting and separate playback timing/EOF-drain debt remain documented in `astra.md`.

## luxepolkit 0.1.0+gtk4-5+luxe11 — focus the ready password entry

- 27 September 2026: Restore keyboard focus after the asynchronous polkit request enables the password field, including retries. Previously session startup disabled the field and moved focus away; enabling it later did not restore focus. No DebInstall rebuild or authorization-policy change is needed.
- Delayed-prompt regression fails before the fix and passes afterward. Release and ten repeated ASan/UBSan runs cover focus, identity selection, retry and cancellation; a separate private-X client verifies pointer/keyboard grabs and release. DEB payload, desktop and no-RPATH checks pass. Promoted luxe11 with luxe10 as the sole prior DEB; nothing installed or authenticated on the live session. RPM verification remains blocked by missing `rpmbuild`.

## luxe-icons 2026.09.09-5 — original application icons

- 27 September 2026: Add original editable SVG artwork for Lucid (blue paper and pen), PCManager (teal folder), Shotty (coral capture viewfinder), Frame (amber landscape), and Matinee (violet playback tunnel). Shared geometry and opaque colors work in light and dark mode. Preserve the separate light theme, dark inheritance, upstream artwork and licenses, and generic icon names.
- Resolve `luxe:` aliases from the checked-in artwork and track SVG changes as build dependencies. Include artwork attribution and update coverage for the two Matinee aliases previously added to source: 26,132 unique names and 29,559 SVG entries excluding the duplicate light tree. Add real GTK lookup checks across all three themes, five sizes and two scales, alongside coverage/content checks.
- Both icon CTests and lookup against the extracted DEB pass; artwork rendered and inspected on light/dark surfaces. Payload audit passes; release 5 promoted with release 4 retained as the sole prior package. RPM generation attempted but blocked by missing `rpmbuild`. Nothing installed.

## frame luxe9 / shotty luxe7 / pcmanager luxe14 — application icon identity

- 27 September 2026: Frame and Shotty launchers and windows now request `org.luxe.Frame` and `org.luxe.Shotty`; PCManager window/About now request `pcmanager`, matching its launcher. This lets Luxe Icons provide dedicated artwork without changing generic icons used by other apps. Lucid and Matinee already use their application identities and need no rebuild. Build and stage only; installation remains the user's step.
- Fresh Release builds, Frame/Shotty private-X behavior tests, four PCManager non-GUI checks, desktop validation and all three DEB payload audits pass. Promoted with Frame luxe8, Shotty luxe6 and PCManager luxe13 retained as the sole prior versions. No functional application logic changed; existing PCManager deprecation warnings remain.

## luxepolkit 0.1.0+gtk4-5+luxe10 — restore password-dialog input grab

- 25 September 2026: Restored the LXDE-style secure input behavior for password prompts. On X11, the authentication dialog now takes an all-or-nothing server-side pointer and keyboard grab when mapped, confines the pointer to the dialog, and releases both grabs on success, failure, cancellation, dialog destruction, and session-start failure. Wayland/non-X11 sessions retain the normal modal focus behavior because they do not permit global input grabs.
- Added the X11 development dependency, Release/dialog lifecycle coverage, and a clean package audit. The existing retry, cancellation, masked-entry, and single-instance paths remain intact. Promoted `luxepolkit_0.1.0+gtk4-5+luxe10_amd64.deb` (SHA-256 `2919dea5a39e12df8b839271ad43ce25082cb3f2fd3407e70da8233d69d5ad9c`); `luxe9` is retained as the prior artifact. Nothing was installed during verification.

## matinee 0.1.0-1+luxe4 — video-only clock and final playback cleanup

- 25 September 2026: Video-only playback now advances the transport clock from decoded presentation timestamps instead of remaining at `0:00`. The engine uses the stream time base for pacing, routes video packets away from the audio demux path, coalesces GTK frame delivery, resets decoder/scaler ownership between tracks, and drops queued ALSA samples before joining workers. Stop, seek, pause/resume, restart, and track switching are cancellable and no longer wait for media to finish.
- The audio-only visualizer is capped at 30 fps, avoiding unnecessary main-loop work. Release and ASan/UBSan tests pass; private-X checks cover paced video, audio/video playback with an ALSA null sink, immediate Stop, restart, and track switching without sanitizer errors. The DEB payload audit and clean-RPATH check pass. Promoted `matinee_0.1.0-1+luxe4_amd64.deb` (SHA-256 `7bec0da58e3f3b38129a528d49a1bf8ae0f385675deeac9187636f7e259b1e23`); `luxe3` is retained as the prior Matinee artifact. Nothing was installed during verification.

## matinee 0.1.0-1+luxe3 — responsive playback and safe transport controls

- 25 September 2026: Fixed playback lag and freezes. Video timing now uses the decoder's best-effort timestamp with the stream time base, so frames are paced in presentation order instead of decoding as fast as possible. For audio/video files the audio thread owns the demuxer and routes video packets to the video thread, preventing the audio path from consuming every video packet; video delivery callbacks are coalesced so the GTK thread cannot accumulate a frame backlog.
- Stop, pause/resume, seek, and track changes now cancel decoder waits and drop queued ALSA samples before joining workers. Decoder contexts, frames, resamplers, scalers, and packet queues are reset between tracks, eliminating the old drain/join and stale-packet paths that could block the UI until media ended. The visualizer is capped at 30 fps so it cannot starve the UI.
- Release and independent ASan/UBSan behavior tests pass. Private-X checks cover video-only pacing, audio/video playback with an ALSA null sink, immediate Stop, restart, and switching tracks; the switch regression also passes under ASan without a double-free. The rebuilt DEB payload audit and clean-RPATH check pass. Promoted `matinee_0.1.0-1+luxe3_amd64.deb` (SHA-256 `613021bbb230f23eff0770578dfd079a4d5db20418422b1629a9f4e4921d327c`); `luxe2` is retained as the prior Matinee artifact. Nothing was installed during verification.

## matinee 0.1.0-1+luxe2 — startup and playback-surface fixes

- 25 September 2026: Fixed the empty-window/startup failure. The application now registers its generated GResource before creating the window, the GtkBuilder template uses the underscore IDs required by `gtk_widget_class_bind_template_child()`, and the overlay child is explicitly typed so the `GtkPicture` is actually parented. Removed the invalid nested template styling and moved the remaining style classes to code. The transport range controls now use GTK signals instead of the unsupported `action-name` property, and application accelerators are installed after the window exists.
- Fixed the video hand-off race: a protected pending texture transfers ownership to the main thread instead of replacing a `GObject` data pointer from the decoder thread. Video is scaled as RGBA and the visualizer uses the matching four-byte texture format, with explicit `GBytes` ownership. The overlay now has a minimum playback height, so video frames render in the surface instead of leaving a white area. Audio-only playback renders the feedback visualizer in that same surface.
- Fresh `Unix Makefiles` Release and independent Debug ASan/UBSan builds pass the Matinee behavior suite (playlist, engine-open, visualizer); the private-X smoke tests show the window remains open, video frames appear for a video-only MP4, and the audio-only visualizer appears with an ALSA null sink. The DEB payload audit passes with no RPATH/RUNPATH. Promoted `matinee_0.1.0-1+luxe2_amd64.deb` (SHA-256 `a245eb9a9e5b5e0d42a3077cfe7385383afb72288767a814a4683e951fccdc2a`); `luxe1` is retained as the sole prior Matinee artifact. Nothing was installed during verification.

## matinee 0.1.0-1+luxe1 — new package

- 25 September 2026: Added Matinee, the suite's media player, built directly on FFmpeg's libraries with no GStreamer dependency. An audio thread decodes, resamples once to stereo S16 and writes to ALSA, whose drain position is the master clock; a video thread decodes and schedules frames against it, and both read one demuxer under a mutex. FFmpeg's own slice threading stays enabled for frame-level parallelism.
- One `GtkPicture` serves as the only playback surface: video frames when the file has a video stream, the visualiser when it does not. Decode threads marshal to the main thread and a newer frame supersedes a pending one, so no GTK work happens off-thread. The visualiser is a feedback tunnel written from the plasma-displacement technique rather than any circulated source, rendered at 320x240, driven by a 1024-point FFT over the mono tap taken from the audio already being sent to the device. Bass drives displacement, mids the hue, treble the highlights, and per-band floors keep a tunnel drifting during silence. A track change resets the feedback buffer so no stale smear survives a seek.
- The playlist is a `GListModel` with in-place reordering that keeps the selection on the same entry. The library is a deliberately dumb on-demand directory walk: bounded to 12 levels and 20000 files, refusing symlinks and special files, with no file monitor and no database. Volume and library path persist in `org.luxe.Matinee`. Application id is `org.luxe.Matinee`; window and About follow the Adwaita header-bar conventions with a suggested-action play button and no second configuration store.
- `/matinee/playlist`, `/matinee/engine-open` and `/matinee/visual` generate a real 16-bit PCM WAV in-process, so the suite carries no binary fixtures. Coverage includes list-model agreement with the accessors, non-wrapping navigation, selection-follows-reorder, clean failure on a missing file, the 440 Hz tone landing in the expected FFT bin, and the silence floor still producing a texture. Fresh `Unix Makefiles` Release and independent Debug ASan/UBSan builds pass all three with no compiler warnings; tests never open a sound device. DEB payload, mode, dependency, RPATH, desktop-entry and schema audits pass; `libav*61` and `libasound2t64` arrive automatically through the shlib scan. Promoted as the 24th Luxe package with no prior artifact. Nothing was installed and no live desktop, sound device, or user file was touched.

## frame 0.1.0-1+luxe8 — WebP support

- 25 September 2026: Fixed WebP images being unviewable. Debian's `libgdk-pixbuf-2.0-0` ships no WebP loader, so `GdkPixbufLoader` rejected every `.webp` with "Unrecognized image file format" and `gdk_pixbuf_get_formats()` never reported `image/webp` — even though Frame's own desktop entry has always advertised `image/webp` and the shared MIME database resolves the extension to it. A new `frame/frame-webp.c` decodes WebP through libwebp instead: the load path sniffs the RIFF/WEBP header, reads the file under a 256 MiB bound, and decodes with `WebPDecodeRGBA`. EXIF orientation is read from the container's `EXIF` chunk and applied with the same mapping gdk-pixbuf uses, so rotated camera photos are not displayed sideways. The shared 4096 px display cap still applies; input is bounded to 64 megapixels and fails closed with a clear message beyond that, because this libwebp exposes no scaled decode. `image/webp` is now always a listed content type, so WebP files appear in folder navigation and can be opened. Animated WebP still shows its first frame; that remains future work.
- Added `/frame/webp` production-code coverage: exact lossless round-trip pixels, the EXIF 6 rotation mapping checked corner by corner, the 4096 px cap, folder listing, and a clean failure for a damaged container. Fresh `Unix Makefiles` Release and independent Debug ASan/UBSan builds pass both tests; the staged DEB adds `libwebp7` automatically through the shlib scan, carries no RPATH/RUNPATH, and keeps the upstream GPL-3 notices. `luxe8` is promoted current with `luxe7` retained as the sole prior artifact. Nothing was installed and no live desktop, wallpaper, or user file was modified.

## pcmanager 1.4.0+gtk4-4+luxe13 — Astra audit

- 25 September 2026: Completed the final PCManager audit closure. Configuration, profile, wallpaper, copy, Properties, drag/drop, X11/Wayland, and single-instance boundaries are hardened; `DISPLAY` host text is sanitized before entering the runtime socket path, and total IPC/client limits remain enforced. Build configuration now explicitly requires GLib 2.80, GTK 4.12, and libadwaita 1.5 for the documented Ubuntu 24.04+/Debian 13+ baseline.
- Fresh `Unix Makefiles` Release and independent ASan/UBSan builds pass 5/5 CTests, including the explicitly isolated Xephyr run. Built and promoted `pcmanager_1.4.0+gtk4-4+luxe13_amd64.deb` (SHA-256 `511e4e1eb2a96fb39bee6ba803dedb5918f9598314eb6de497a2bca43c4b1c50`); the payload/mode/conffile/translation/dependency/RPATH audit passes with Ubuntu/Debian GLib alternatives. `luxe12` is the sole prior artifact. Nothing was installed or allowed to modify the live X11, D-Bus, desktop, wallpaper, or user file state. RPM generation remains deferred until Fedora hardware and `rpmbuild` are available.

## pcmanager 1.4.0+gtk4-4+luxe12 — Astra audit (superseded by luxe13)

- 25 September 2026: Completed the final PCManager hardening pass after the initial `luxe11` package. Browser/desktop configuration is bounded and atomic; profile paths are allowlisted; wallpaper files and decoded dimensions are bounded; tiled wallpaper rendering uses a cached Cairo pattern; recursive directory copies are depth-limited, private, and transactional; terminal enumerator errors stop Empty Trash; and X11 desktop calls are protected by GDK error traps with explicit Wayland refusal.
- Properties measurement is cancellable with guaranteed final callbacks, external MOVE cleanup is tracked as a browser operation until completion, drag qdata/highlight ownership is explicitly released, history navigation is range-checked, and single-instance IPC now rejects unexpected connection failures, caps total request bytes/clients, and parses `DISPLAY` defensively. The Xephyr integration test is opt-in, uses a caller-selected unused display, and isolates HOME/XDG/runtime state.
- Fresh `Unix Makefiles` Release and independent ASan/UBSan builds pass 5/5 CTests, including the explicitly isolated Xephyr run. Built and promoted `pcmanager_1.4.0+gtk4-4+luxe12_amd64.deb` (SHA-256 `058cb0c9e615f1e2671b56224db81019186afbb64f37c5dcaa37c476ae52bfab`); the payload/mode/conffile/translation/dependency/RPATH audit passes with Ubuntu/Debian GLib alternatives. `luxe11` is the sole prior artifact. Nothing was installed or allowed to modify the live X11, D-Bus, desktop, wallpaper, or user file state. RPM generation remains deferred until Fedora hardware and `rpmbuild` are available.

## pcmanager 1.4.0+gtk4-4+luxe11 — Astra audit (superseded by luxe12)

- 25 September 2026: Completed the PCManager GTK4/X11 audit. User and system desktop/browser configuration now uses bounded no-follow reads, profile validation, ownership/mode checks, and durable same-directory writes; wallpaper loading rejects unsafe/special/oversized inputs. Single-instance IPC display parsing is checked, runtime paths are bounded, malformed client records are rejected, and history navigation is range-checked.
- Preserved the existing GIO/libfm file-operation behavior, conflict/cancellation handling, embedded terminal launch, X11 wallpaper desktop, GTK4 lifecycle, upstream notices/licenses, and 55 compiled translations. Fresh `Unix Makefiles` Release and independent ASan/UBSan builds pass 5/5 CTests, including the Xephyr drag/drop integration test. The DEB payload/modes/conffiles/dependencies/notices/clean-RPATH audit passes with t64/non-t64 GLib alternatives for Ubuntu and Debian.
- Built and promoted `pcmanager_1.4.0+gtk4-4+luxe11_amd64.deb` (SHA-256 `27b5ffac82997745033d395b9815c33dd490842a1f6733807db390d9f6e14144`); `luxe10` is the sole prior artifact. Nothing was installed or allowed to modify the live desktop, X11, D-Bus, or user files. RPM generation remains deferred until Fedora hardware and `rpmbuild` are available.

## luxurystore 0.1.0-15 — Astra audit

- 25 September 2026: Completed the dual-ABI LuxuryStore audit for Ubuntu APT 6 and Debian APT 7. The unprivileged UI and privileged helper now use bounded request validation, checked direct argv execution, explicit polkit authorization, package-lock revalidation, and stale-plan rejection. Repository definitions are bounded/root-owned regular files, semantic source parsing is used, and rewrites are same-directory atomic with rollback after a failed APT refresh.
- Fixed fork/wait status handling, child/resource cleanup, APT cache/view ownership, lock release, XDG path mutation, file-description and package-description bounds, empty blacklist/store behavior, Flathub cache races/staleness, UI/store asynchronous teardown, store-only null handling, and GUI model/depcache synchronization. Removed the unnecessary root `deborphan` probe from the default backend path. Added always-active/root-safe tests, dual UI/helper ABI checks, ABI-6 and ABI-7 backend runs, malformed request and repository-write regressions, and an out-of-tree UI harness.
- Fresh `Unix Makefiles` Release and independent ASan/UBSan dual-ABI builds pass 5/5 CTests, including both backend test binaries. Built and audited `luxurystore_0.1.0-15_amd64.deb` (SHA-256 `3fa7199c76eadc275bb929c8ec130935f496eeae05e1f2b80c98bd579bff6211`); payload modes, configured policy/helper path, ABI dependencies, notices/licenses, and clean RPATH pass. `0.1.0-15` is current and `0.1.0-14` is the sole prior artifact. Nothing was installed or allowed to modify live APT, Flatpak, Snap, D-Bus, or desktop state. RPM generation remains deferred until Fedora hardware and `rpmbuild` are available.

## luxesession 0.5.5+gtk4-4+luxe18 — Astra audit

- 25 September 2026: Completed the LuxeSession source and payload audit. Session/profile names and autostart/config paths are validated; configuration and desktop-entry reads are bounded and reject symlinks/FIFOs/NUL/oversized data; settings and default-app persistence use same-directory atomic writes. XSettings list values, keyboard repeat settings, and monitor replacement are bounded and lifecycle-safe. Command construction now uses direct argv parsing, empty-command rejection, serialized child reloads, PID start-time checks, and identity-safe logout signaling.
- Hardened the default-app log, D-Bus error paths, ConsoleKit/logind contracts, update-check timeout/parser/lock handling, default-app toggles, child exit status, category blacklist matching, desktop-entry encoding, asynchronous database scan accounting, and X11 clipboard `INCR`/`MULTIPLE` ownership and bounds. Migration and XDG autostart retain user settings and reject special files. Configured wrapper/default paths are generated by CMake. Added six isolated CTests covering migration boundaries, XDG parsing, profile/PID-start validation, XSettings list limits, and atomic file behavior; fresh Release and independent ASan/UBSan builds pass 6/6 with sanitizer leak detection disabled for process-lifetime X11/GTK allocations.
- Built and audited `luxesession_0.5.5+gtk4-4+luxe18_amd64.deb` (SHA-256 `d818290ada63e39eac0d8fecd8ee7471baaa4e9fd967b2c6c9847ed0af7bf7c9`); upstream licenses/notices, 64 compiled translations, root modes, dependencies, clean RPATH, and conffiles pass review. `luxe18` is current and `luxe17` is the sole prior artifact. Nothing was installed or allowed to modify the live session. RPM generation remains deferred until Fedora hardware and `rpmbuild` are available.

## luxepanel 0.10.1+gtk4-8+luxe25 — Astra audit

- 24 September 2026: Completed the LuxePanel source and payload audit. Configuration and line parsing now reject oversized/special/malformed input, preserve strings through escaped atomic writes, and expose checked numeric/structure errors. X11 property reads, GTK4 panel teardown, autohide/geometry, Run-dialog autocomplete and close lifecycle, drag-and-drop, taskbar/tray/task-button ownership, hardware/status timers, notification D-Bus replies, hotkey unbinding, ALSA failure cleanup, controller boundaries, and plugin/resource teardown are hardened without removing existing panel behavior.
- Default profiles no longer seed unavailable `pager`/`netstat` plugins or a nonexistent `my-computer.svg`; the existing `my-computer.png` asset is used. Added source-data validation, line/configuration, hostile X-property, notification, default-panel, Run-dialog close, taskbar overflow/activation, controller respawn, and volume integration regressions.
- Fresh `Unix Makefiles` Release and independent ASan/UBSan builds pass 10/10 CTests. The staged DEB payload, root ownership, deterministic modes, translations/notices, dependencies, and clean RPATH audit pass. `luxe25` is promoted current with `luxe24` retained as the sole prior artifact; nothing was installed. RPM generation remains deferred until Fedora hardware and `rpmbuild` are available.

## luxedm 1.4.1-1+luxe10 — Astra audit

- 24 September 2026: Remove the packaged root-console default, fail closed on empty-password compatibility authentication, validate PAM identities/responses, and copy NSS/shadow data before releasing lookup state. Root commands and optional configuration values are parsed as bounded direct argv data; malformed quoting and NUL-bearing input are rejected.
- Harden config, session, image, log, instance-lock, and Xauthority paths against symlinks, special files, replacement races, broad modes, stale files, oversized input, malformed colors/coordinates, and X11 interpolation/resource failures. Preserve deterministic multi-monitor rendering and transactional image replacement.
- Make lock-panel startup fail closed, defer greeter signal cleanup to the main loop, handle interrupted waits, check DPMS results, release XKB atom names, and release X-backed panel resources before closing the display. Migration and Debian default-manager helpers now use atomic same-directory writes and symlink refusal; the locker remains an ordinary per-user executable.
- Align LuxeDM `luxe10` package/compiled/manual versions, preserve all upstream notices and theme credits, and expand behavioral/source-boundary tests. Fresh `Unix Makefiles` Release and independent ASan/UBSan builds pass 9/9 CTests; staged DEB payload, conffile/mode, shell, and clean-RPATH audits pass. `luxe10` is promoted current with `luxe9` retained as the sole prior artifact; no live installation or desktop/PAM/service mutation occurred. RPM generation was attempted but remains blocked because `rpmbuild` is unavailable.

## luxeappearance-obconf 0.2.3+gtk4-4+luxe6 — Astra audit

- 23 September 2026: Fix fatal-startup/close/activation lifetimes, custom Openbox config-target fallback, stale X11 property handling, and the borrowed `ObtPaths` list/preview teardown corruption. Apply now uses bounded same-directory serialization, stat-based concurrent-change detection, symlink refusal, fsync and atomic rename.
- Harden theme/archive workflows: validate theme names and nested paths, run checked extraction asynchronously, reject traversal/links/special members/oversized output, validate UTF-8, clean partial claims, and create archives without replacing an existing destination. Font, desktop-name, EWMH-property and preview geometry bounds are explicit.
- Align the `luxeappearance-obconf` gettext domain, configured install paths, SPDX/credits, clean-RPATH build, private-X lifecycle and installer regressions. Release and independent ASan/UBSan builds pass 2/2 CTests; staged DEB payload, desktop validation and `readelf -d` inspection pass. `luxe6` is promoted current with `luxe5` retained as prior; no live installation or desktop mutation occurred. RPM generation remains blocked because `rpmbuild` is unavailable.

## luxeappearance 0.3.0+gtk4-2+luxe11 — Astra audit

- 23 September 2026: Fix repeated activation/global-builder lifetime bugs, close teardown ordering, late D-Bus and icon-install callbacks, and the X11 server-grab freeze risk. Add a real activation/reopen lifecycle test.
- Make Apply transactional across GTK 3/4, LuxeSession/standalone gtkrc, and Openbox; preserve unrelated settings and comments, validate malformed/NUL/symlinked inputs, repair theme/session state, and report save failures without discarding rollback errors.
- Harden theme enumeration/install/remove: user themes take precedence, GTK/cursor sorting and validation are correct, installed themes are removable from their actual XDG root, archive extraction is asynchronous and rejects unsafe/special members, and selection/cleanup no longer leak or use stale widgets.
- Align GPL-3 licensing metadata/SPDX with the shipped license, require gettext, use configured GNUInstallDirs paths, validate desktop entries, explain libadwaita styling ownership in the UI, and retire stale ELF/.gmo artifacts recoverably.
- Fresh clean-RPATH Release and ASan/UBSan builds pass both CTests (persistence/rollback/probe/lifecycle plus archive-installer safety) in private X/D-Bus. DEB payload audit and desktop validation pass; `luxe11` is promoted current with `luxe10` retained as prior, and no live installation occurred. RPM verification remains blocked because `rpmbuild` is unavailable.

## lucid 0.1.0-1+luxe8 — Astra audit

- 23 September 2026: Fix NULL-unsafe overwrite/close answers, keep Cancel defaults with busy-guarded Save-As re-entrancy, and balance save-target ownership. Report oversized dirty buffers from named sessions instead of silently downgrading/skipping them.
- Harden shutdown: cancel surviving document/window work, stop directory monitoring, reset quit/snapshot flags, NULL window widgets, and guard close/remove/quit paths. Sidebar now requests `standard::file` with bounds checks and a parent-relative fallback.
- Convert Sessions, Preferences, Syntax and Go-to-Line to Adwaita dialogs; gate test-only `LUCID_DATA_DIR` behind `LUCID_TEST`; update stale Meson/runner docs to CMake/RunNestedX. Preserve all gedit/modeline/Tinted-Theming notices and 20 schemes.
- Release and ASan/UBSan both pass 2/2 CTests in private X/D-Bus, covering recovery, dialogs, sidebar and oversized reporting. DEB audit passed; luxe8 current with luxe6 prior. No live installation. RPM unavailable.

## luxeterm 0.3.2+gtk4-2+luxe8 — Astra audit

- Replace custom socket transport with per-display GApplication command forwarding; preserve caller cwd, literal argv and independent `--no-remote` windows. Reject empty commands and invalid dimensions; preserve login-shell arguments and avoid command-title out-of-bounds access.
- Replace shared/global preferences with dialog-owned Adwaita pages and safe color-preset updates. Consolidate settings load/save/copy/free, own all strings/keyfiles, preserve unknown keys and reject malformed/symlink overwrite; write atomically and report save errors.
- Fix stale rename/confirmation/tab/spawn callbacks, duplicate child watching, popup/menu ownership, typed Alt+n targets, initial tab placement, legacy word-selection grammar and configured paths. Retain legacy bold/F10 values and explain their lack of effect in current VTE/GTK4.
- Private-X behavioral and real process-forwarding tests pass Release and ASan/UBSan, including clipboard paste, dialog/child closure, pending spawns, settings preservation and independent sessions. No live session changes. Application `src/*.[ch]` falls from 3,313 to about 2,490 lines (about 25%); retired sources/generated files remain recoverable outside the tree.
- Local luxe7 package was superseded before promotion to preserve per-display single-instance isolation; luxe8 is the distribution candidate. RPM testing remains unavailable.

## luxepower 0.2.0-6 — Astra audit

- Fix brightness +/- callback crashes, serialize brightness requests, cancel pending changes on device selection, and replace invalid terminal-container casts with a shared VTE activity area.
- Own asynchronous window/result lifetimes, suppress updates after close, prevent duplicate windows and overlapping loads, preserve custom combo values, and fix staging descriptors and TLP duplicate-assignment rendering.
- Harden helper identity/file/hash validation, reject FIFO/symlink/NUL input, bound stats arguments, scrub the environment, check durable writes and report logind reload failure. Update only the selected lid action while preserving the remaining drop-in settings; read vendor/runtime precedence and correct lid defaults.
- Fix idle XScreenSaver cleanup, lock-file ownership/descriptor handling and screensaver-on timing. Add configured helper paths, full About version, copyright notices and a bounded lock interval.
- Four behavioral CTests pass in Release and ASan/UBSan (leak detection disabled), including private-X/VTE lifetime and idle tests, real owned-child capture, malformed staged files and rendering stability. No live power settings, root actions, screen lock or package installation were exercised. RPM verification remains unavailable locally.

## luxup 0.1.1-1+luxe3 — Astra audit

- 23 September 2026: Fix short Flatpak row access, cancel owned command groups with a bounded termination fallback, report refresh failures as unknown, and return failure from unsuccessful headless checks. Preserve queued refresh requests across cancellation.
- Migrate the normal GUI/About/errors to libadwaita, show full version, start the spinner, implement Quit, disable review during checks and safely finish cancellation at shutdown. Preserve XEmbed integration while recreating destroyed tray windows, watching manager loss, scoping X errors to the private display and repainting symbolic icons for theme/mode changes.
- Preserve Snap-policy ownership after failed reversal, refuse changes after failed status queries, use configured helper paths and a dedicated systemd unit directory, and share the DEB/RPM install script. Deterministic private-X/package-response/policy tests pass Release and ASan/UBSan; no live package-manager or policy action was performed.

## frame 0.1.0-1+luxe7 — Astra audit

- 23 September 2026: Fix repeated activation, application/window ownership, load/transform pixbuf leaks, late file-dialog callbacks, EXIF task completion, missing metadata handling and immutable file/dimension snapshots. Decode incrementally off-thread with the existing 4096px display cap requested before allocation.
- Match trash completion to the file actually trashed, keep navigation stable and clear empty views/pending loads. Fix pinch/drag signal signatures, scope gesture state per window, avoid duplicate slideshow timers, coalesce monitor reloads and use display-safe filenames.
- Adopt Adwaita headers/information dialog and normalized spacing, bound schema settings and align GPL-3 SPDX with retained licensing. Production-code tests pass Release and ASan/UBSan, including pixels/dimensions, transforms, gesture signals, metadata failure, late callbacks, wallpaper argument handoff and simulated trash completion. Replaced obsolete smoke helpers recoverably; no live files were trashed or wallpapers changed.

## luxetask 0.1.9+gtk4-1+luxe6 — Astra audit

- 23 September 2026: Bind process signals to pidfds and checked start times; Term now sends SIGTERM, right-click selects the pointed row, and priority values are validated. Refresh recognizes PID reuse, exec/name and ownership changes.
- Validate proc records, retain 64-bit CPU/memory counters, use actual elapsed time and clock ticks, bound memory arithmetic and refresh settings, and preserve unknown/malformed configuration with checked durable writes.
- Fix repeated activation, shortcut/model ownership and dialog state. Use typed numeric columns and native sorting, consolidate row lookup/full-path refresh, and adopt Adwaita preferences/destructive confirmations. Release and ASan/UBSan production-code tests pass; no installed packages changed.

## luxerandr 0.3.2+gtk4-2+luxe8 — Astra audit

- 23 September 2026: Replaced the disconnected/dangling confirmation timer with a single owned 15-second Adwaita confirmation. Timeout, close and Quit revert once; Apply/Save cannot overlap a pending confirmation. Snapshot exact current monitor offsets before applying, and attempt rollback after partial command failure too.
- Fixed inactive Quick Options signals, repeat activation, startup-error lifetime, primary/zero-mode/preferred-marker parsing, process-wide locale mutation, disabled placement anchors, all-off saved layouts, unsafe tokens and malformed X property handling. Window recovery now asks the WM to move frames rather than moving clients in frame-relative coordinates.
- Simplified command construction and parser/dialog code, normalized header/spacing/install paths, and added private-X production-code tests with simulated xrandr execution. No live display configuration was changed. Supported deprecated combo widgets remain migration debt.

## shotty 0.1.0-1+luxe6 — Astra audit

- 23 September 2026: Fixed delayed-capture and pixbuf ownership, duplicate activation, failed-save/failed-viewer exits, asynchronous save lifetime, and temporary viewer-file cleanup. Clipboard copying requests persistence before exiting and stays available to paste when no manager retains it.
- Capture the actual client with optional frame extents, resolve pointer fallback through WM_STATE, reject invalid geometry/display inputs, and correct cropped/premultiplied cursor compositing. Added schema ranges, Adwaita header/dialog chrome, normalized spacing, and aligned SPDX metadata with the preserved GPL-3 upstream license.
- Replaced stale precompiled/false-success smoke helpers with production-code tests for UI lifetime, saving, geometry, cursor pixels and actual slop region selection. Release and ASan/UBSan pass. Nested-X tests now run in an unmapped host window to avoid user-input interference. No live packages installed.

## luxenotify 0.1.0-4 — Astra audit

- 23 September 2026: Validate raw-image alpha/channel consistency and last-row bounds, decode file icons asynchronously at display size, and release unused icon/action widgets. Correct safe hyperlink rendering, preserve notification-count continuity on replacements, and coalesce layout work with explicit teardown cleanup.
- Use overflow-safe workarea clipping and re-read desktop workareas during positioning. Correct full-version/About provenance. Private-X/D-Bus tests now exercise actual image decoding, markup labels, actions, expiry, ID wraparound, layout coalescing and hostile workarea properties in addition to protocol smoke coverage.

## luxepolkit 0.1.0+gtk4-5+luxe9 — Astra audit

- 23 September 2026: Fixed early/threaded cancellation, synchronous session-callback lifetime hazards, missing login-session/UI error handling, and recursive authentication-startup retries. Password responses are copied, the entry cleared before dispatch, and the temporary copy wiped afterward. Cancel stays usable while authentication is pending.
- Modernized listener registration/unregistration, removed unused request state, normalized prompt spacing and install paths, and moved lifecycle verification onto private X/D-Bus with source UI. Release and ASan/UBSan tests exercise simulated authentication only; the running agent was not replaced.

## luxe-icons 2026.09.09-4 — Astra audit

- 23 September 2026: Preserve dotted/reverse-DNS application names when merging Numix coverage into Papirus. Restores 2,805 full SVG names while removing 116 erroneous truncated names; all pinned upstream names remain available. Light/dark drawings are unchanged, and the light theme remains independent.
- Write the success stamp only after coverage validation, track coverage metadata as a build dependency, guard output cleanup, consolidate identical cache scripts, and correct artwork license metadata. Added full source-name/index/link/light-copy and failure-boundary tests; updated measured coverage to 26,130 unique entry names and 29,555 SVG entries excluding the duplicate light tree.

## luxemenu-data 0.1.4+luxe-2+luxe6 — Astra audit

- 23 September 2026: Replaced the host-dependent menu check with a complete isolated XDG fixture tested by the real private menu-cache generator. It verifies every hidden settings launcher, Settings/Store visibility, and ordinary third-party entries on clean builders. Existing menu behavior is unchanged; build, directory validation and integration test pass.

## luxeinput 0.3.5-1+luxe5 — Astra audit

- 23 September 2026: Cancel now restores the exact live X pointer mapping/acceleration and keyboard repeat settings captured on opening. Repeat activation presents the existing window. Keyboard-layout launch is asynchronous and no longer leaks executable discovery results.
- Consolidated settings loading, preserved malformed/unreadable configuration, and made save errors visible and retryable. Adopted the Adwaita header and a resizable default window. Added a private-X behavioral test covering cancellation, persistence, duplicate activation, and failed saves; fresh build/test passes without compiler warnings. No live input settings changed during verification.

## luxesettings 0.1.0+luxe7 — Astra audit

- 23 September 2026: Added the Bible-required About action with full release provenance and guarded missing GSettings schema sources/keys on minimal systems. Added actual isolated application startup, single-window activation, About and live light/dark tests alongside expanded invalid-catalog tests. Fresh build passes both CTests without compiler warnings. No settings tools are launched or live preferences changed by the tests.

## luxehotkey 0.1.0+gtk4-2+luxe5 — Astra audit

- 23 September 2026: Fixed dangling Openbox XML nodes during repeated shortcut edits/removals, alternate-key merging after reload, command-mask lookup, nested-action metadata preservation, stale GUI pointers after rollback, action-option ordering/escaping, numeric validation, and X connection/property cleanup. CLI help no longer initializes GTK or requires a display.
- Shared the binding update logic and private libfm/menu-cache sources. Replaced mirrored/grep tests with actual implementation tests; added isolated GTK editor lifecycle coverage and Adwaita editor chrome. Fresh Release and ASan/UBSan builds pass all four CTests. RPM generation remains blocked by missing `rpmbuild`; no live configuration or package installation was performed.

## debinstall 0.1.0-7 — Astra audit

- 23 September 2026: Bound asynchronous installation confirmation to the reviewed package and blocked overlapping opens/confirmations. Generate the launcher, helper, policy, and full About version from CMake installation paths; correctly load the configured module under Python isolated mode. Removed redundant helper-path probing. Adopted Adwaita's header bar and the Bible's group spacing.
- Replaced source-grep privilege checks with behavioral tests for caller validation, policy binding, environment cleanup, changed-plan refusal, and nonblocking FIFO rejection. UI tests now allocate their own nested X display and temporary state. All three CTests passed; installed isolated-mode imports, custom lib64/helper paths, desktop metadata, and DEB payload checked. RPM generation is unverified because `rpmbuild` is absent. Promoted release 7, retaining release 6 as the single prior DEB; neither was installed into the desktop.

## Shared private source consolidation — Astra audit

- 23 September 2026: Consolidated three byte-identical libfm/menu-cache copies into one repository-private `internal/` subtree. Removed obsolete copy-synchronization tests. All licenses remain with the shared sources; no public libraries or headers are introduced. Original duplicate trees are recoverable outside the repository and in the pre-audit source archive. Consumer builds and full reviews remain in progress.

## luxesession 0.5.5+gtk4-4+luxe17 / luxeappearance 0.3.0+gtk4-2+luxe10

- 23 September 2026: Fixed the old-style Openbox frame on server-decorated secondary windows such as Remmina connections. Added separate flat Syscrash-Luxe dark and light themes with matched GTK surface colors and unobtrusive window buttons, leaving Debian's stock Syscrash and the prior Syscrash-Light untouched. Luxe Appearance now selects the matching variant with its system light/dark toggle; new session defaults use the dark variant and larger, regular-weight title text. Existing user Openbox settings are preserved until Appearance is applied.

## luxemenu-data 0.1.4+luxe-2+luxe5

- 22 September 2026: Restored LuxuryStore to the application menu. Its installed, valid desktop file had mistakenly been included in the Settings-hub exclusion list, making the package manager unreachable except by command line. The menu-cache integration test now requires both Luxe Settings and LuxuryStore to be present.

## luxepanel 0.10.1+gtk4-8+luxe24

- 22 September 2026: Fixed menu categories reopening immediately when clicked to close. Header rows no longer take GTK's generic list-row activation path in addition to their disclosure gesture; Enter and Space continue to toggle the focused header directly.

## luxurystore 0.1.0-14

- 22 September 2026: Unified Debian 13/Ubuntu 24.04 package. LuxuryStore now ships private UI and authenticated-helper pairs linked independently to `libapt-pkg.so.6.0` and `libapt-pkg.so.7.0`; lightweight public launchers select the installed ABI before either ELF binary loads. The DEB declares an APT runtime alternative rather than an impossible shared SONAME. Both ABI links have artifact-level CTests.

## luxurystore 0.1.0-13

- 22 September 2026: The CI/release artifact targets Ubuntu 24.04 and is linked to `libapt-pkg.so.6.0`. APT changes its library SONAME between Debian 13 and Ubuntu 24.04, so one directly linked binary cannot honestly support both packages through a relaxed dependency. CMake now prefers the installed system development ABI (Ubuntu Actions selects 6.0) while retaining the checked-in Debian headers only as a fallback for local runtime-only development systems.

## luxedm 1.4.1-1+luxe8 / luxurystore 0.1.0-12

- 22 September 2026: Ubuntu 24.04 compatibility packaging. LuxeDM now declares the ABI-compatible `libpng16-16t64 (>= 1.6.43)` floor rather than inheriting Debian 13's newer build-host minimum. LuxuryStore can build against either supported APT ABI: its Ubuntu 24.04 build uses extracted Noble headers and `libapt-pkg.so.6.0`, declares `libapt-pkg6.0t64`, and does not pretend a Debian 13 `libapt-pkg.so.7.0` binary can run on Ubuntu.

## luxepanel 0.10.1+gtk4-8+luxe23

- 22 September 2026: Application-menu categories now begin closed. Their open/closed choices are kept in the running panel only, so reopening the menu preserves them while logging out or restarting the panel returns to the compact default. Both mouse and keyboard toggles update the disclosure arrow immediately; a focused CTest covers the session-state store.

## luxemenu-data 0.1.4+luxe-2+luxe4

- 22 September 2026: Luxe Settings is now the sole Settings-menu entry for Luxe configuration. The menu layout excludes the individual Appearance, Openbox, desktop wallpaper, input, display, power, Hotkey, and session/default-applications launchers while leaving their desktop files and direct commands available to the hub. A menu-cache integration test verifies both exclusion and the retained Luxe Settings entry.

## debinstall 0.1.0-5

- 21 September 2026: After a successful install, the primary action becomes an enabled red **Close** button. It closes the completed installer window and its private package snapshot instead of leaving a disabled “Installed” control or inviting an accidental reinstall. Opening another package restores the normal blue Install/Reinstall action.

## luxesettings 0.1.0+luxe6

- 22 September 2026: Added Luxe Settings, a native GTK4/libadwaita control-center hub. It provides one modern entry point for Appearance, Openbox decorations, Desktop, Panel, input, displays, power, Hotkey, default applications, startup applications, and LuxuryStore while continuing to launch each existing, tested settings module directly. The launch catalog is validated as direct argument vectors and covered by a CTest; it never uses a shell or introduces a second settings store. Fixed the initial launch crash by using `adw_application_window_set_content()` for the Adwaita window and escaping markup-aware row titles. Its light/dark state now follows Luxe Appearance’s `color-scheme` through `AdwStyleManager` and live GSettings changes. Style-manager setup now runs after GTK has initialized its display, avoiding the early-startup display access that caused the first correction build to abort. The hub now describes Desktop accurately as wallpaper configuration and makes virtual-desktop controls discoverable under Window Decorations.

## Source and test debt cleanup

- 21 September 2026: Removed disconnected Autotools inputs, generated Makefiles, configure output, and autom4te caches from the five CMake-owned components Luxe Appearance, Openbox Appearance, Luxe RandR, Luxe Session, and Luxe Terminal. The components now have one authoritative build system. Removed the two remaining source-text CTests in the appearance tools; retained tests exercise real settings persistence, session migration, and RandR command construction.

## luxepanel 0.10.1+gtk4-8+luxe22

- 21 September 2026: Light mode now selects a dedicated light Luxe start button while retaining the existing dark artwork unchanged. GTK4 XEmbed tray frames derive their background from the active scheme, so ParentRelative tray clients no longer bring a hardcoded dark tile into a light panel.

## luxepolkit 0.1.0+gtk4-5+luxe8

- 21 September 2026: The long-lived authentication agent now follows `org.gnome.desktop.interface` `color-scheme` changes published by Luxe Appearance, updating existing and future authentication dialogs between light and dark modes without requiring a session restart.

## luxeappearance 0.3.0+gtk4-2+luxe9

- 21 September 2026: Adds an integration assertion that the panel reload command is issued after Apply only inside LuxeSession. Carries the panel system-theme Apply fix and CMake-only source/test cleanup.

## luxerandr 0.3.2+gtk4-2+luxe7 / luxesession 0.5.5+gtk4-4+luxe16

- 21 September 2026: CMake-only source/test cleanup release; no user configuration or installed runtime assets were removed.

## luxeterm 0.3.2+gtk4-2+luxe6

- 21 September 2026: Restored reliable right-click Paste. LuxeTerm now claims a secondary click before VTE's legacy primary-selection handler, focuses the clicked terminal, and opens its clipboard context menu directly.

## luxeterm 0.3.2+gtk4-2+luxe5

- 21 September 2026: Verified and packaged the CMake-only source/test cleanup after restoring the required GTK4 VTE and Openbox development dependencies.

## luxeappearance 0.3.0+gtk4-2+luxe7

- 21 September 2026: Applying the System color mode now restarts an already session-managed LuxePanel after GTK, icon, and Openbox settings persist. This makes a panel using its System theme background immediately adopt the selected light or dark appearance without ever spawning a panel outside a Luxe session.

## luxesession 0.5.5+gtk4-4+luxe15

- 21 September 2026: Deepened Syscrash-Light from near-white into an approximately 20%-black-tinted light-grey palette. Title bars, menus, button faces, inactive chrome, and borders now carry the same more substantial neutral surface while preserving the light mode's dark-label contrast and blue active accent.

## luxeappearance 0.3.0+gtk4-2+luxe6

- 21 September 2026: The System color mode control now switches the current user's Openbox theme atomically with GTK and icons: Light selects Syscrash-Light and Dark selects Syscrash. It updates only `theme/name` in `~/.config/openbox/rc.xml`, preserves all other Openbox settings, seeds from the packaged session template only for a new user, and rolls all settings back if persistence fails.

## luxesession 0.5.5+gtk4-4+luxe14

- 21 September 2026: Added `Syscrash-Light`, a separate light-grey Openbox companion to Syscrash. It preserves the compact window geometry and maximisation artwork while providing light neutral chrome, dark labels, and a steel-blue active accent; the dark Syscrash theme supplied by Openbox remains untouched.

## luxe-icons 2026.09.09-3 / luxeappearance 0.3.0+gtk4-2+luxe5

- 21 September 2026: Added a separately installed `Luxe-Light` icon theme beside the existing `Luxe` compatibility base and `Luxe-Dark`; no existing artwork is recolored or replaced. Luxe Appearance now provides a Light/Dark selector that applies the coherent pairs Adwaita + Luxe-Light and Adwaita-dark + Luxe-Dark, while preserving the detailed theme pickers for custom choices.

## luxepanel 0.10.1+gtk4-8+luxe21 / luxenotify 0.1.0-3

- 21 September 2026: Added a default built-in Notifications panel indicator. It is blank with no active notifications and displays a vibrant circled exclamation point while a LuxeNotify card is waiting. LuxeNotify now exports a narrow active-count-only D-Bus interface to the panel; no notification content leaves the service through that interface.

## luxesession 0.5.5+gtk4-4+luxe13 (supersedes luxe12)

- 21 September 2026: The logout dialog is the fixed, lowercase-brand prompt “Logout luxe session.” It no longer exposes the internal LXDE compatibility profile or the OS release, and no longer runs `lsb_release` to obtain that irrelevant information.

## pcmanager 1.4.0+gtk4-4+luxe10 (supersedes luxe9)

- 21 September 2026: Added a GIO-backed removable-media surface to Places. Mounted volumes show an unmount or eject action; mountable unmounted volumes are visible and can be mounted from the row or its action. PCManager observes volume arrivals and sends a standard desktop notification for mountable media. The build and all six existing PCManager CTests pass; physical-media behavior remains a release-test check.

## lucid 0.1.0-1+luxe6 (supersedes luxe5)

- 20 September 2026: Fix startup abort from installing an `AdwToolbarView` with `gtk_window_set_child()` on an `AdwApplicationWindow`; use `adw_application_window_set_content()` instead. Session-state and editor-integration tests pass, a direct Xephyr launch maps the Lucid window, and the DEB payload audit passes.

## Artifact layout (19 September 2026)

- Latest audited DEBs now live in `artifacts/debs/`; one available previous version per package lives in `artifacts/old/`. The organizer compares Debian package versions, updates checksums and the manifest, and removes the former `test/` and nested `older/` directories. The complete-suite collector uses the same organizer.

## luxepanel 0.10.1+gtk4-8+luxe20 (supersedes luxe19)

- 19 September 2026: The GTK4 taskbar reports a shrinkable minimum width and clips excess buttons, keeping the clock, tray, and volume on the panel when many windows are open. Task buttons retain a usable minimum width. New Xephyr layout regression and taskbar click tests pass; DEB payload audit passes.

## pcmanager 1.4.0+gtk4-4+luxe9 (supersedes luxe8)

- 19 September 2026: Disconnect the Properties dialog's destroy callback when its asynchronous size calculation finishes, and suppress UI updates after the dialog closes. The multi-selection Xephyr regression covers closing after size appears and closing while measurement is running; DEB payload audit passes.

## luxurystore 0.1.0-11 (supersedes 0.1.0-10)

- 19 September 2026: Added a header-bar Repos picker for configured APT vendor sources, with independent enable controls. A disabled source contributes a discoverable package row; Mark install asks to enable it, refreshes APT in the authenticated helper, then marks the actual package for dependency review. Source edits are limited to named files, reject symlinks and missing scoped signing keys, and use atomic writes. Chrome appears as unconfigured when its source file is absent; unsigned ROCm nightly can be turned off but not enabled from LuxuryStore. Backend/UI tests pass and DEB payload audit passes.

Compiled from the project documentation (`luxenew.md`, `LUXE-BIBLE.md`, `greport.md`, `GREPORT-ASSESSMENT-PLAN.md`, `DISTRO.md`, `artifacts/UNRELEASED.md`). Entries are listed oldest to newest within each package: the initial GTK4 port or creation first, then the September 2026 assessment-remediation work in step order (Steps 1–18 of the assessment plan). Per-change dates are not documented for most entries; doc-level dates appear in parentheses where the sources give them.

All 22 independently buildable packages are covered. On 14 September 2026 the set became 23 packages: `tlpui` left the set and `luxepower` replaced it (historical entries below keep their dates).

---

## Suite-wide (all packages)

- Ported the inherited LXDE/GTK2 desktop applications to a coherent GTK4 suite for X11 and Openbox: current GLib/GIO APIs, Adwaita-compatible layouts, dark-theme presentation, Luxe branding, `lx*` compatibility command aliases.
- Established independent CMake/CPack builds per package (DEB/RPM), correct `/usr`/`/etc`/`/var` paths, desktop-file validation, preserved conffiles, and generated Luxe credit records.
- Moved `libfm`/`menu-cache` to private static subsets inside LuxeHotkey, LuxePanel, and PCManager; no public library packages.
- Added fresh-user defaults and additive migration (no overwriting of user configuration) and focused integration tests.
- 8 September 2026: tray plugin added to the panel configuration; TLP/TLPUI package set confirmed for the distribution plan.
- 12 September 2026: package catalog updated (`luxenew.md`); 22 DEBs collected at 15.79 MB download footprint.
- Assessment Step 1 — artifact safety: one authoritative component list feeds selection and collection; CPack normalizes directory (0755) and regular-file (0644) modes and rejects every set-ID payload; CI audits each DEB before upload; collection is transactional (staged, verified, prior set restored on failure). Existing bundle marked development-only.
- Assessment UI-policy decision: normal Luxe application windows now have a scoped mandatory-libadwaita rule in `LUXE-BIBLE.md`.
- Assessment distro catalog: NodeSource recorded as a disabled-by-default, explicit-major repository candidate in `DISTRO.md`.
- Assessment Step 17 — shared build hardening and clean rebuild: `luxe_configure_package` now validates every shipped desktop entry at build time; new `luxe_install_gschema()` adds the Debian schema trigger plus RPM `glib-compile-schemas` scriptlets; all 22 components rebuilt from clean trees with `BUILD_TESTING=ON`; every registered test suite passed; every DEB passed the payload audit; complete 22-package set (15.12 MB) re-collected with `manifest.json` and verified SHA256SUMS; no group-writable directories or set-ID payloads remain. P0 exit criteria met.
- 13 September 2026: `artifacts/UNRELEASED.md` marks the current artifact bundle as development-only; a public release must replace all 22 packages in one audited collection.

---

## debinstall 0.1.0-2 (superseded by 0.1.0-3)

Original GTK4 application by Jerry Howell using `gdebi-core` and python-apt; no GDebi GTK frontend code bundled.

- Created: local-DEB inspection and install flow, dependency reporting, PolicyKit-authenticated helper, unprivileged GUI, captured GTK4 VTE output, PCManager integration, About/credits, data-only CMake packaging for the Python application.
- Step 12: previously unwired Python backend test now runs under CTest; new helper-boundary audit added (O_NOFOLLOW private copy, PKEXEC_UID ownership re-verification, scrubbed environment, SHA-256 plan-token binding, admin-authenticated policy bound to the fixed helper path, root-side refusals).
- 16 September 2026 (remediation pass): robust inspection-JSON parsing (last non-blank line + decode errors); explicit file-list truncation marker (`files_truncated`); `Gtk.AlertDialog` replaces deprecated `MessageDialog`; helper path via constant with prefix fallback. Backend tests (9) + boundary audit pass.

## frame 0.1.0-1+luxe3 (superseded by 0.1.0-1+luxe4)

Original GTK4 image viewer by Jerry Howell.

- Created: folder browsing, zoom and fit modes, rotation, slideshow, EXIF display, trash, wallpaper handoff to PCManager, keyboard navigation, Luxe styling and icon, About/credits, CMake packaging, image-viewer integration tests.
- Step 2: corrected the Info dialog double-free; made error-banner timeout source ownership explicit and removed pending sources during disposal; split trash work from image-load cancellation with strong window ownership for trash and wallpaper callbacks; guarded empty open requests.
- Step 2: the Makefile-backed smoke harness became a CMake/CTest test that accepts the built binary; smoke test passed in Xephyr normally and under ASan/UBSan; fresh DEB passed the payload audit.
- 16 September 2026 (remediation pass): batched `next_files_async` folder enumeration (100/batch, cancellable, generation-guarded); downscale moved to a GTask worker; perpetual fit-mode tick replaced by `notify::allocation` refit. Xephyr smoke passes.

## lucid 0.1.0-1+luxe2 (superseded by 0.1.0-1+luxe3)

Selective redevelopment: new GTK4 editor frontend built inside the imported Debian Gedit 48.1 source ecosystem; Gedit and plugin attribution retained.

- Created: GTK4, GtkSourceView 5, GTK4 VTE, and libspelling instead of the older plugin frontend; tabs, undo/redo, search/replace, go-to-line, syntax highlighting, zoom, statistics, sorting, date/time insertion, modelines, selection highlighting; async load/save preserving encoding and line endings; modified-document Save/Discard/Cancel handling; file-browser sidebar, spell checking, Session Saver, embedded terminal, color picker integrated directly; named sessions and periodic recovery snapshots under XDG state; twenty credited light/dark color schemes; independent command, desktop entry, schema, and state so Gedit is untouched; CMake packaging and state/session testing.
- Step 13: Discard now rewrites recovery state immediately so a crash inside the snapshot interval cannot resurrect discarded edits; first-time Save As onto an existing document now asks Replace/Cancel instead of silently overwriting; restored sessions skip URIs already open or already restored in the same pass; recovery snapshot writes (serialize/fsync/rename) moved to a GTask worker with a generation counter so a text change during the write keeps the dirty flag set — one write in flight at a time.
- Step 13: the previously meson-only `editor-test` is registered as the `editor-integration` CTest in an isolated Xephyr session, covering save/discard/recovery/dedup plus the new overwrite confirmation; the legacy `meson.build` remains as a non-authoritative reference for P2 removal.
- 16 September 2026 (remediation pass): failed-load refreshes the failed tab's label; exact active-page restore (dedup-safe, clamped); ~6 s keystroke-quiescence snapshots with bounded session size; 500k-char live-highlight guard; `meson.build` marked non-authoritative in-file; dead `luxe-dialogs.h`/`luxe-spacing.h` deleted. Session-state + editor-integration pass.

## luxe-icons 2026.09.09-2

Curated flat icon-theme assembly from pinned Papirus and Numix Circle archives; full upstream artwork licenses and author records retained.

- Created: combines upstream coverage into the Luxe Dark theme, adds aliases and Luxe application icons for previously blank menu entries, installs icon-theme inheritance and cache hooks, builds reproducibly from checked-in hash-pinned archives through pure CMake logic; keeps every unique icon name, pixel-tunes small controls/status art, collapses redundant size variants into scalable SVG coverage, repairs aliases crossing removed directories, generates caches only on the target system, XZ-compressed Debian payload.
- 16 September 2026 (remediation pass): `index.theme` restores per-directory semantics (pixel dirs `Type=Fixed` with exact Min/MaxSize); `SOURCE_DATE_EPOCH` mtime normalization verified (zero files newer than epoch); build-time coverage-count assertion (23441 unique names / 26866 SVG entries; `coverage.json` corrected from stale 23439/26866, `DISTRO.md` updated).

## luxeappearance 0.3.0+gtk4-2+luxe2 (superseded by 0.3.0+gtk4-2+luxe3)

Port of LXAppearance by Hong Jen Yee (PCMan) and Ying-Chun Liu.

- Ported the appearance dialog and live preview from GTK2 to GTK4; consolidated GTK theme, icon theme, font, cursor, color-scheme, and toolbar settings; retained icon-theme installation and X11 settings compatibility; Luxe branding, settings-menu integration, About/credits, CMake packaging, `lxappearance` compatibility command.
- Step 14: Apply now refuses to synthesize a bare `desktop.conf` when no session template exists (that file launches the session's core components); the icon-theme installer stages extraction under `$TMPDIR`, cleans up on exit, quotes all paths, verifies archive contents, and refuses to silently replace an existing theme.
- Step 14: the real save-settings test became the `save-settings-preserve` CTest (seeded `XDG_CONFIG_DIRS` template exercising the new guard) plus the `luxeappearance-p1-workflows` audit; fresh DEB passed the payload audit.
- 16 September 2026 (remediation pass): UTF-8-preserving theme-name quoting with self-test; gtk-3.0/gtk-4.0 snapshot rollback on Apply failure; idle-deferred enumeration with hash dedup; fully async D-Bus session probe. Both CTests pass.

## luxeappearance-obconf 2:0.2.3+gtk4-4+luxe2 (superseded by 2:0.2.3+gtk4-4+luxe3)

Port of the LXAppearance Openbox plugin (derived from ObConf by Dana Jansens and others).

- Ported the ObConf interface, previews, theme handling, archives, and Openbox configuration path to GTK4; added usable virtual-desktop controls; preserved Openbox theme previews and lineage; Luxe naming, menu categories, About/credits, CMake packaging, `obconf` compatibility command.
- Step 14: theme-archive extraction now stages in a private temporary directory and only renames a verified `openbox-3/themerc` into place; refuses to silently replace an existing theme; initialized the previously-read-uninitialized spawn exit codes; Close button routes through the window's close-request path so `obconf_cleanup()` runs; detects `rc.xml` changes since load so Apply cannot silently overwrite edits written by LuxeHotkey or another tool (refuses with a reload hint, real config path for the stamp); theme picker `mkdir` tightened to 0755; selection-cleared NULL read fixed.
- Step 14: `obconf-p1-workflows` audit passes; fresh DEB passed the payload audit.
- 16 September 2026 (remediation pass): 200 ms trailing debounce for title-layout preview (teardown-safe); dead GTK2/3 branches removed; RSVG via `PkgConfig::RSVG` imported target with shared-only guard for openbox libs; reload-on-Apply contract re-verified intact.

## luxehotkey 0.1.0+gtk4-2+luxe2 (superseded by 0.1.0+gtk4-2+luxe3)

Port of LXHotkey and its GTK and Openbox plugins from the LXDE project.

- Ported the shortcut editor plugin interface to GTK4 keeping the Openbox binding backend; builds exact private `libfm`/`menu-cache` subsets instead of shipping library packages; Luxe naming, settings-menu integration, About/credits, CMake packaging, `lxhotkey` compatibility command.
- Step 9: copies of shortcut option metadata now retain both `has_actions` and `has_value`, and metadata comparison checks both shape flags; the Openbox plugin parses its serialized XML before writing `rc.xml`; reports an unavailable X display rather than dereferencing it; GTK/Openbox modules link their internal libfm dependency explicitly under CMake.
- Step 9: new `luxehotkey-openbox-safety` CTest enforces metadata preservation, pre-write XML validation, and display-null handling.
- 16 September 2026 (remediation pass): literal backslash escape grammar with symmetric serializer + `luxehotkey-roundtrip` CTest; `<Primary>` mapped to Control both directions; two-phase binding commits (stash+restore); GNUInstallDirs plugin/locale paths; autotools remnants purged. 2/2 CTests pass.

## luxeinput 0.3.5-1+luxe2 (superseded by 0.3.5-1+luxe3)

Port of LXInput by Hong Jen Yee.

- Ported keyboard and pointer configuration to GTK4 preserving the direct X11 settings path; updated layout, labels, assets, desktop integration, settings categories; About/credits, CMake packaging, `lxinput` compatibility command.
- Step 15: reports failed saves/spawns; frees the spawn output; restores the live X bell state on Cancel (captured with `XGetKeyboardControl` at load); clamps the `DESKTOP_SESSION`-derived config path against traversal.
- 16 September 2026 (remediation pass): pointer remap queries real button count via `xmodmap -pp` (extras preserved); CWD `.ui` fallbacks removed (installed datadir only).

## luxemenu-data 0.1.4+luxe-2+luxe1 (superseded by 0.1.4+luxe-2+luxe2)

Adaptation of LXMenu Data from the LXDE team.

- Adapted the XDG application-menu hierarchy and category definitions to the Luxe desktop identity; retained compatibility with LXDE-aware launchers and autostart metadata; converted to a small reproducible CMake/CPack data-only package with complete attribution.
- 16 September 2026 (remediation pass): dead autotools tree deleted (29 files); RPM `Provides`/`Obsoletes` `lxmenu-data` mirrored.

## luxedm 1.4.1-1+luxe4 (superseded by 1.4.1-1+luxe5)

Fork of Debian's maintained SLiM 1.4.1-2, preserving all upstream and Debian contributor credits.

- Reworked the display manager into the branded Luxe login and native session-lock screen with PAM authentication and shared theme; fixed the obsolete systemd `-s` command mismatch; deterministic session choice preferring the packaged Luxe session over bare Openbox; F1 session-cycling hint; attaches secondary GPU providers and enables every connected RandR output before drawing, with resolution-aware backgrounds and synchronized login panels on every monitor; cloned outputs share one logical surface; autologin resolves the configured session before launch; authenticated desktops registered with logind as X11 user sessions; manager's private X connection no longer leaks into the desktop; user sessions get their own process group and logout replaces the manager process cleanly; X cookies from kernel secure randomness, `xauth` without a shell, advisory-locked PID/lock files without symlink following; corrected PAM response ownership and wiped password buffers; lock helper authenticates the real calling UID; refuses a misleading lock screen unless both keyboard and pointer grabs succeed, restoring display state on ordinary termination signals; hardened image decoding (bounded dimensions, checked allocations, normalized PNG, recoverable JPEG) with regression tests; preserves setuid and conffile metadata in packages; integrates with Debian's default-display-manager selection and `luxedm-set-default` PolicyKit route.
- Step 7: `luxedm-lock` converted to a normal user executable so X11/theme/PAM code never runs with elevated credentials; instance lock now belongs to the calling user in `XDG_RUNTIME_DIR` with a verified `/tmp` fallback; set-ID payloads prohibited globally at package and audit time; signal handlers only record `sig_atomic_t` state; the raise thread stops and joins before X resources are destroyed; virtual-terminal switching remains disabled rather than granting the lock UI root access.
- Step 7 verification: rebuilt DEB contains `usr/bin/luxedm-lock` at mode 0755 with no set-ID payload and passes the shared payload audit; LuxeDM passes its version, core, service-command, and migration-security CTests.
- 16 September 2026 (remediation pass): all 6 root `system()` sites converted to argv fork+exec (no shell; `grep system\(` clean); image bound 4096 px + 64 MiB budget; F11 root screenshot removed; new `luxedm-root-shell`, `luxedm-pam-conversation`, `luxedm-grabs` CTests — 7/7 pass. RandR hotplug while greeter/lock is up documented as future work.
- 17 September 2026 (lock-session diagnosis): three same-day session restarts traced to abrupt X-server death, not session logic — the dead server logged no clean shutdown and died mid `drmmode_do_crtc_dpms`/framebuffer re-probe while locked; locker itself exits 0 cleanly. `luxedm-lock` no longer force-enables DPMS: it only shortens timeouts while DPMS is already enabled and always restores them, removing Luxe's forced contribution to the killer transition. 7/7 CTests pass; refreshed DEB re-audited into `artifacts/` (manifest + SHA256SUMS updated).

## luxenotify 0.1.0-1 (superseded by 0.1.0-2)

Original GTK4 freedesktop.org notification service by Jerry Howell.

- Created: ownership and D-Bus activation of `org.freedesktop.Notifications`; stacked X11 notification windows in the primary monitor's usable work area outside taskbar and pager; application and raw-image icons; safe body markup; action buttons and default clicks; progress values; replacement IDs; urgency, resident, expiration, and explicit-close behavior; session autostart; `--about` credits view; isolated D-Bus/X11 smoke test.
- Step 6: image hints now validate channels, stride, dimensions, checked byte totals, and a 16-MiB cap before allocation; file icons must be bounded regular files; validated file-icon decoding is asynchronous so notification layout is not blocked by image I/O.
- Step 6 verification: D-Bus smoke test passed; fresh DEB passed the payload audit.
- 16 September 2026 (remediation pass): markup whitelist sanitizer (href http(s)/mailto only); `persistence` capability dropped; defined close reason 3 for replacements; cached workarea with monitor subscriptions; smoke test extended (urgency/images/resident/hostile markup) and passing.

## luxepanel 0.10.1+gtk4-8+luxe16

Port of LXPanel (descended from fbpanel and fspanel), component-level upstream credits retained.

- Ported the panel and configuration interfaces from GTK2 to GTK4; preserved and integrated menu, launchers, taskbar, XEmbed system tray, clock, workspace indicator, ALSA volume, CPU, battery, keyboard LED, keyboard layout, thermal, CPU-frequency, and monitor plugins; fixed taskbar activation; corrected tray ownership, docking, allocation, and geometry resynchronization for foreign tray icons; `luxepanelctl restart` respawns a missing panel and reports undeliverable commands; volume plugin interactions (slider/mute/Pavucontrol); removed stale GTK2-era messages; builds private menu-cache/libfm subsets; tests for restart, taskbar, tray, menu, and volume.
- Step 5: corrected the monitor-ring resize tail offset; rejects negative and over-64-KiB tray balloon lengths before allocation; replaced the battery alarm's detached thread with asynchronous process launch so teardown cannot free its semaphore in use; GTK4 task-menu actions safely no-op when their target disappears; corrected the internal libfm include path needed by menu-cache-gen's CMake target.
- Step 16: corrected the `XQueryPointer` misuse that fed window-relative pointer coordinates into autohide geometry and monitor detection (root coordinates now used); the tray's deferred geometry resync traps and flushes the GDK connection the frames actually live on; panel config saved atomically (mkstemp + fflush + fsync + rename with error reporting); thermal parser no longer underflows on short sysfs lines; cpufreq probe opens each CPU's own directory and no longer leaks one GDir per CPU; the `_LUXEPANEL_CMD` ClientMessage is read through the correct `xclient.message_type` union member; root-pixmap background paint runs under a GDK error trap so a WM-restart `BadPixmap` cannot abort the panel; task-window input selection registers on every live private connection so destroying one taskbar plugin no longer deafens survivors.
- Step 16: integration tests registered in CTest (`taskbar-click`, `ctl-respawn`, `volume-integration` over Xephyr/Openbox/bwrap), which surfaced and fixed three build defects: the panel executable now exports symbols for loadable plugins (`ENABLE_EXPORTS`), the static core archive no longer garbage-collects plugin-API symbols (whole-archive link), and the build no longer depends on a stale autotools `config.h` (generated from `config.h.cmake`); libfm's embedded module loader no longer warns about an absent default modules directory when application-specific directories are registered; stale autotools artifacts, binaries, and `icon-grid-old.c` removed.
- Step 17: clean-tree rebuild with all CTests passing; fresh DEB passed the payload audit.
- 16 September 2026 (remediation pass): tray `_XEMBED_INFO` verification (id-0/dead refused; legacy absence logged, still docked — documented); `_LUXEPANEL_CMD` opcode allowlist + rate limit + validation with trust-boundary comment; menu rows freed on close; polls relaxed (cpu/monitors/autohide). 3/3 CTests pass.

## luxepolkit 0.1.0+gtk4-5+luxe6 (supersedes luxe5; superseded by 0.1.0+gtk4-5+luxe7)

Port of LXPolkit by Hong Jen Yee (PCMan).

- Ported the PolicyKit authentication agent to GTK4 as the suite's `polkit-1-auth-agent` provider; replaced the account selector with a clear label for the PolicyKit identity; masked password field, compact action buttons, safe focus, inline retry feedback; corrected agent startup and single-instance registration so duplicate autostart paths cannot crash on a second D-Bus export; Luxe session integration, About/credits, independent CMake packaging.
- Step 15: fails the authentication request cleanly instead of crashing when the UI file is missing or corrupt; falls back to polkit's identity string when a passwd/group entry is absent; clears the password entry immediately after submission; completes the retry deterministically instead of leaving polkit's caller hanging when no restartable identity exists; session start extracted into a reusable helper; the dialog-lifecycle test registered in CTest; the dead second agent tree in luxesession and the stale autotools tree/config.h deleted; the desktop file uses a current icon.
- 16 September 2026 (verify-only pass): dead trees confirmed absent; 8 more generated byproducts deleted; dialog-lifecycle passes; single-instance guard confirmed by inspection.

## luxerandr 0.3.2+gtk4-2+luxe3 (superseded by 0.3.2+gtk4-2+luxe4)

Port of LXRandR by Hong Jen Yee and the LXDE developers.

- Ported display configuration to GTK4 retaining direct XRandR control; multi-monitor layout, resolution and refresh configuration, and restoration of the saved arrangement during the session; Luxe settings-menu categories and icon, About/credits, CMake packaging, `lxrandr` compatibility command; integration work with LuxeDM isolates live resolution changes from the greeter and lock lifecycle.
- Step 8: saved display entries now use a direct, Desktop-Entry-valid `xrandr` command rather than a single-quoted `sh -c` wrapper; connector, mode, and refresh-rate tokens constrained to a safe grammar before becoming process or desktop-entry arguments; Apply and restore check the child exit result and display failures; save reports filesystem errors.
- Step 8: new `luxerandr-autostart-command` CTest prevents the shell wrapper from returning and requires child-status checking; fresh DEB passed the payload audit.
- 16 September 2026 (remediation pass): eDP/EDP/DSI recognition; enabled-aware anchor chaining (external-only/disabled-anchor/3+ fixed); zero-mode bounds checks; 5 new CTests — 6/6 pass.

## luxesession 0.5.5+gtk4-4+luxe8 (superseded by luxe10; see luxe9 joint section and P1 pass below) (superseded by luxe10; see luxe9 joint section and P1 pass below)

Port of LXSession by Hong Jen Yee (session manager based on XSM), with retained GNOME/Xfce/Vala contributor credits.

- Ported the session tools, logout UI, default-app settings, autostart editor, database frontend, and clipboard integration to GTK4; introduced the `X-Luxe` desktop identity with LXDE as internal compatibility profile; added `start-luxe`, the packaged Xsession entry, and deterministic startup of Openbox, LuxePanel, LuxePolkit, LuxeNotify, the settings daemon, clipboard, and XDG autostart services; wired `luxelock` and `Super+L` to the native `luxedm-lock` screen; fresh-user defaults for Adwaita Dark, Luxe Dark icons, Afterpiece, GTK3/GTK4 settings, Openbox, and the panel; additive idempotent migration; consolidated startup into core `desktop.conf` services and XDG autostart layers; migrates the retired line-based autostart file to XDG entries with backup; LuxeSession Settings made the sole autostart interface (enable/disable/add/remove), duplicate page removed from Default Applications; fixed immediate reversible enable toggles and blank-command rejection; preserved LXSession aliases and compatibility locations; one CMake target graph plus an XDG-migration regression test.
- Step 4: corrected both command-log loop bounds; made polkit shutdown depend on the actual object rather than a configuration string; corrected the XMODIFIERS variable name in the maintained Vala source and generated C; bounded logout helper formatting and reads so a trailing NUL always fits.
- Step 4 verification: full CMake build and registered autostart-migration test passed; fresh DEB passed the payload audit.
- 16 September 2026 (remediation pass): WM spawn-failure fallback via `SpawnError` catch + signal-death fallback; `WIFEXITED/WEXITSTATUS` decoding everywhere (stale `case 256` removed from generated C); 750 ms settings-reload debounce; `XDG_CONFIG_HOME` fallback; sorted deterministic autostart order; idle-deferred laptop-detect/language/power/network launches; `lock_manager` wired. Migration test passes.
- Step 18: fixed the session window-manager spawn regression — a stray empty argument (`openbox ""`) from the advanced `desktop.conf` form no longer reaches openbox: the unset-session default contributes no argument and empty tokens are filtered from split commands; the session wrapper additionally snapshots `run.log` and `.xsession-errors` to `.prev` at each login so failed logins keep their evidence.

## luxetask 0.1.9+gtk4-1+luxe2 (superseded by 0.1.9+gtk4-1+luxe3)

Port of LXTask (derived from Xfce4 Task Manager by Johannes Zellner and Álvaro Lopes).

- Ported the process list, controls, and resource display from GTK2 to GTK4; updated Linux process collection and interface for current Luxe behavior and styling; Luxe naming, menu integration, About/credits, CMake packaging, `lxtask` compatibility command.
- Step 15: guards every signal action (not just STOP) against pid 1, itself, and its whole ancestor chain via `/proc` walking; renice is now a direct `setpriority()` call instead of a `sh -c` shell.
- 16 September 2026 (remediation pass): integer PID/PPID columns with hash row lookup; persistent uid→name cache; atomic config write; NaN guards with clamping.

## luxeterm 0.3.2+gtk4-2+luxe2 (superseded by 0.3.2+gtk4-2+luxe3)

Port of LXTerminal, upstream contributor credits retained.

- Ported the terminal and preferences interface to GTK4 and the GTK4 VTE API; retained tabbed operation, preferences, URL matching, and single-instance/socket behavior; fixed X11/window title metadata so the taskbar shows LuxeTerm; Luxe icon, desktop integration, About/credits, CMake packaging, `lxterminal` compatibility command; PCManager actions say "Open in LuxeTerm".
- Step 3: close-all no longer dereferences a terminal after its final tab destroys the window; terminal records stay alive through pending VTE spawn callbacks and cancel those spawns during tab/window teardown; the per-user socket accumulates a bounded request until EOF, rejects incomplete/malformed/oversized input, and bounds arguments before parsing.
- Step 3 verification: built and packaged from the declared VTE API; malformed and fragmented socket clients exercised in Xephyr normally and under ASan/UBSan; fresh DEB passed the payload audit.
- 16 September 2026 (P0 remote-content→command fix): both URL launch paths (right-click "Open URL" and Ctrl+click) replaced shell-based `g_strdup_printf("xdg-open %s", url)` + `g_spawn_command_line_async` with a `terminal_open_url()` helper that prefers `g_app_info_launch_default_for_uri()` and falls back to argv `{"xdg-open", url}` via `g_spawn_async()` — terminal output can no longer reach a shell. Window/terminal titles are escaped with `g_markup_escape_text()` before Pango markup setters (plain `set_text` on tab-activate), closing the Pango restyle vector; fixed the `g_strconcat` leak in the asterisk-prefix path. Verified with `gcc -fsyntax-only` against the declared VTE/GTK4 headers (exit 0, no errors) plus a standalone harness proving argv spawn passes shell metacharacters literally and markup is escaped.

## luxup 0.1.1-1+luxe1

Original GTK4 tray update notifier by Jerry Howell.

- Created: tray notification of APT updates and updates from installed Flatpak or Snap channels, with handoff to LuxuryStore for review and application; Luxe session integration, About/credits, and independent CMake packaging. Its package enables the `luxup-snap-policy.path`/`luxup-snap-policy.service` pair that applies Snap's supported global refresh hold while continuing to enumerate pending Snap revisions into LuxuryStore; removing LuxUp releases only a hold LuxUp created.
- 16 September 2026 (remediation pass): distinct unknown-state tray icon (no false-green); stale results discarded on failed recheck; real cancellation; `--refresh` forwarded through single-instance; bounded re-dock backoff re-armed on tray events. Snap-policy test passes.

## luxurystore 0.1.0-7 (superseded by 0.1.0-8)

Selective redevelopment: Synaptic's proven APT backend and package model (full Synaptic contributor history retained) under a purpose-built GTK4 frontend.

- Created: replaced the inherited GTK frontend; PolicyKit authentication required when the store opens with the GUI unprivileged and the helper re-checking authorization; all marked APT changes resolved as one plan under the package lock; multi-package removals with shared dependencies resolved together with transient Protected flags cleared between plans; green plus / red minus marks; transaction output captured in a scrollable embedded terminal; search, detail, install, removal, purge, upgrade, apply/cancel, progress, and optional Flatpak/Snap adapters; About/credits, Luxe styling, CMake packaging, and backend tests for authentication denial, unprivileged plans, shared-dependency removals, protected-flag cleanup, marks, and progress.
- Step 12: a failed APT cache load now re-enables search and source controls instead of bricking the window; review windows are serialized (one at a time, guard reset on apply and destroy); the privileged helper holds the package-manager archive lock across resolve/plan/commit so a concurrent APT or dpkg transaction cannot change the state a reviewed plan was computed from; store searches debounced (350 ms) so Snap `find` no longer fires per keystroke.
- Step 12 verification: backend test plus the new `luxurystore-p1-workflows` audit pass; fresh DEB passed the payload audit.
- 16 September 2026 (remediation pass): full-cache Protected-bit iteration; asserted `child-exited` invariant; single-flight Review confirmed; per-keystroke query tokenization; off-thread resolve with busy gating; consent-gated Flathub remote-add with session cache; GTK-free helper (ldd-verified). Backend + workflows pass.

## pcmanager 1.4.0+gtk4-4+luxe5 (superseded by 1.4.0+gtk4-4+luxe6)

Port of PCManFM by Hong Jen Yee and Ingo Brückl, with retained ExoIconView, libfm, and menu-cache contributor histories.

- Ported the file manager and wallpaper desktop mode to GTK4: Places, bookmarks, tabs, icon/detail views, filtering, hidden files, navigation, Properties, Open With, asynchronous directory monitoring, disk-used/free display, complete trash handling with Empty Trash, repaired drag and drop (same-filesystem moves, cross-filesystem/external copies), right-click Extract Here via Xarchiver, local DEB handoff to DebInstall, LuxeTerm action naming, wallpaper-only desktop mode without icons/thumbnails, private libfm/menu-cache, CMake packaging, and smoke/integration coverage.
- Step 10a: malformed local IPC requests now fail per client instead of calling GLib's process-aborting `g_error`; request lines capped at 64 KiB, argument count at 256, screen numbers parsed with range and syntax checks, incomplete requests discarded without invoking the application callback.
- Step 10b: drops flag the drag as internally handled so `drag-end` can no longer delete files a drop already moved, copied, or trashed; an external destination accepting MOVE triggers a Trash (never delete) of the source files with failures reported; copy/move batches no longer abort on the first conflict — existing destinations are skipped without overwriting, other failures counted, cancellation stops the batch immediately, and a summary error is returned when anything was skipped or failed; the header contract documents the policy.
- Step 11: pasting into the Trash tab now trashes the sources; monitored directory changes no longer wipe the user's place — selection is re-matched by basename after each model rebuild and the viewport anchor row is scrolled back into view; the wallpaper desktop rebuilds its per-monitor windows on display hotplug (debounced 500 ms, clean monitor-model ownership); grid rows guarded against exporting a file a mid-press model rebuild moved elsewhere.
- Verification: CTests (`single-inst-safety`, `file-ops-conflicts` headless incl. ASan/UBSan, `drag-move-completion`, `p1-workflows` wiring audit, and the Xephyr `drag-drop` suite extended to 11 scenarios including external-MOVE and conflict-refusal) pass; fresh DEB passed the payload audit.

## shotty 0.1.0-1+luxe2 (superseded by 0.1.0-1+luxe3)

Port of Simpleshot GTK by Tom Zaluckij, with the bundled/libslopy lineage credited to Dalton Nell and the slop project.

- Ported the interface from GTK3 to GTK4 and renamed the application for Luxe; entire-screen, active-window, and selected-region capture with delay and post-capture actions; corrected active-window capture behavior with automated coverage; desktop integration, icon, About/credits, independent CMake packaging.
- Step 15: hand-off now writes via `g_mkstemp` into `~/.cache/shotty` (0700), unlinks after the viewer opens it, and closes the descriptor; the capture-delay timer owns a window reference that dispose releases; the cursor checkbox actually draws the cursor (XFixes image blended source-over); the NULL app-info path is guarded.
- Step 15 verification: Xephyr smoke test passes in CTest (`shotty-smoke`, exercised including the new cursor drawing) with a fresh audited DEB.
- 16 September 2026 (remediation pass): cached preview scaling (re-scale on allocation change only); dead Imgur stub + debug prints removed; smoke harness fixed (`xfixes` link, unset-var, Phase 3) — 1/1 green.

## tlpui 1.10.1-1 (replaced by luxepower)

Intact upstream TLPUI 1.10.1 by d4nj1 and contributors, packaged from pinned commit `82ffaba1246d3a6a22e48854856beb7c3e85eba6`. Deliberately not a GTK4 port and excluded from code grading per project owner decision (12 Sep 2026).

- Created: independent install-only CMake packaging for DEB/RPM, a desktop launcher and dependency metadata, and inclusion in the planned Lumen power-management set alongside TLP and TLP-RDW. No Luxe code changes to the application itself.
- 14 September 2026: removed from the package set; replaced by `luxepower`.

## luxepower 0.2.0-1 (superseded by 0.2.0-5; see also 0.2.0-4 entries)

Original GTK4/libadwaita power manager by Jerry Howell, replacing TLPUI. TLP interaction follows the command surface documented by TLPUI (`tlp-stat -c` parsing, `/usr/share/tlp/defaults.conf` defaults, pkexec for privileged writes); TLP and TLPUI remain separately packaged and credited. First shipped as 0.1.0-1 in Python; re-implemented natively in C11 as 0.2.0-1 to match the rest of the suite (native build, shlibdeps, no python3-gi dependency).

- Created (14 September 2026): single-window libadwaita interface with four pages — Power (curated TLP settings with AC/battery pairs, batch Apply/Discard writing `/etc/tlp.conf` and restarting TLP), Sleep & lid (mask/unmask of suspend, hibernate, and hybrid-sleep targets; effective lid actions from logind.conf and drop-ins, applied via a `/etc/systemd/logind.conf.d/99-luxepower.conf` drop-in), Display (brightness through logind `SetBrightness`, no privilege required; idle timer starting `luxelock` with GSettings persistence), and Status (unprivileged TLP summary plus root-complete statistics).
- Ships `/usr/libexec/luxepower-apply`, the debinstall-pattern privileged helper: pkexec-only with PKEXEC_UID verification, O_NOFOLLOW staging, group/other mode rejection, SHA-256 binding to the reviewed content, strict config and lid grammar validation, atomic writes, and a single `org.luxe.LuxePower.apply` `auth_admin_keep` policy action.
- Ships `luxepower-idle`, a headless XDG-autostart idle monitor for the Luxe session: XScreenSaver-extension idle queries via ctypes (xprintidle fallback), GSettings-driven `lock-enabled`/`lock-minutes`, single-instance lock, and `luxelock` launch with a post-unlock cooldown.
- Provides/Conflicts/Replaces `tlpui` so installation upgrades the package cleanly.
- Ships `luxepower-idle` autostart with `Exec=/usr/libexec/luxepower-idle` (bare names fail the systemd XDG-autostart generator, which is why the idle daemon never started initially).
- 0.2.0-4 (14 September 2026): after a real-session segfault during authenticated applies, all privileged operations were moved to the DebInstall pattern — `pkexec --disable-internal-agent` spawned inside an embedded VTE terminal on the page performing the operation, with the helper's output visible in place and completion detected through `child-exited`. Covers TLP apply, sleep-mask and lid toggles, and root complete-statistics; brightness keeps its direct logind D-Bus path.
- Verified: backend unit tests and helper-boundary audit pass under CTest; desktop entries and GSettings schema validate; DEB built and passed the shared payload audit; window smoke-tested in Xephyr.
- 16 September 2026 (remediation pass): TLP value-grammar hardening (shell metachars rejected); hex-only helper hash; sleep/lid allowlists + caps; backlight path validation; logind brightness errors surfaced as toast + dialog. Backend (6/6) + boundary pass.

---

## pcmanager 1.4.0+gtk4-4+luxe6 (superseded by 1.4.0+gtk4-4+luxe7)

- 15 September 2026: Properties now measures and shows size on disk (du-style, recursive via `g_file_measure_disk_usage` in GTask workers) for directories and multi-selections, with a Contents row (N files, M folders), an item count for multi-select, and a Close button. Previously the dialog only read the selection model's cached `standard::size`, which is empty for directories, and was gated to single-file selection. Verifications: five CTests pass including the Xephyr drag-drop suite; dialog behavior exercised in Xephyr for single file, single folder, and mixed selections; DEB passed the shared payload audit.
- 16 September 2026 (remediation pass): cached collation keys; monitor-size wallpaper decode; per-motion stat cache; copy/move `N of M · %` progress; continue-past-failure empty-trash with summary; `WATCH_MOVES` + rate-limited monitors. Headless 4/4 + Xephyr drag-drop pass.

## luxepanel 0.10.1+gtk4-8+luxe17 / luxesession 0.5.5+gtk4-4+luxe9

- 16 September 2026 (dogfood crash): launching an Electron application followed by a PCManager crash (SIGSEGV) took the panel down with it — the taskbar's icon fetch (`XGetWindowProperty` on `_NET_WM_ICON` for a task window that had just died) raised an untrapped BadWindow X error, which GDK treats as fatal, so the panel exited(1). LuxeSession's exit guard then classified exit status 256 as "Exit normal, don't reload" and left the desktop without a panel.
- luxepanel fix: added shared `task_xtrap_push/pop` X-error trapping and wrapped every untrapped Xlib path that touches arbitrary task windows — `get_wm_icon` (both `XGetWindowProperty` calls, WM_HINTS, `XGetGeometry`), `get_window_monitor`, and `task_raise_window`. A dead client window now costs a logged warning instead of the panel.
- luxesession fix: removed the `case 256` classification from both exit guards so exit(1) — the standard GDK X-error abort path — now reloads the guarded application (bounded by the existing crash-count cap). Normal exit(0) and user SIGTERM(15) still do not reload.
- Verified: panel builds with the trap helpers, three CTests pass, DEB audits; session builds, migration CTest passes, DEB audits. Panel restored in the running session via `luxepanelctl restart`.

## luxedm 1.4.1-1+luxe5 (superseded by 1.4.1-1+luxe6)

- 17 September 2026: `luxedm-lock` no longer force-enables DPMS (see 17 September entry under 1.4.1-1+luxe4 for diagnosis); version bumped per the packaging rule that every bugfix rebuild advances `+luxeN`.

## 18 September 2026 — hreport P0 repair pass

Five packages rebuilt for third-audit P0 findings, each version-bumped per the packaging rule, CTest-verified, payload-audited, and collected into `artifacts/`.

## luxepanel 0.10.1+gtk4-8+luxe18 (supersedes luxe17; superseded by 0.10.1+gtk4-8+luxe19)

- 18 September 2026: `task_xtrap_pop()` now saves and restores the previous X error handler (with a nesting-depth counter so inner traps don't corrupt the saved handler); `_wnck_gdk_pixbuf_get_from_pixmap()` converted to the same push/pop pattern; xkb `system()` replaced with a structured argv `setxkbmap` spawn; dead `alarmProcess()` thread deleted; previously untrapped X property reads on task windows (`task_has_urgency`, `task_set_names`, `get_net_wm_desktop`/`get_wm_state`, `XGetClassHint`) wrapped in the fixed trap. 4/4 CTests pass (taskbar-click, ctl-respawn, volume-integration, libfm-sync).

## luxetask 0.1.9+gtk4-1+luxe3 (supersedes luxe2; superseded by 0.1.9+gtk4-1+luxe4)

- 18 September 2026: `task_pid_is_protected()` rewritten — the 16 Sept version walked the target's ancestors (protecting every process, since all descend from PID 1); it now protects PID ≤ 1, itself, and its own ancestor chain only, so Stop/Cont/Term/Kill work again. `change_full_path()` OOB fixed via PID→index hash matching. New headless `pid-protection` CTest (pid 1, self, live child) passes.

## luxepower 0.2.0-5 (supersedes 0.2.0-4)

- 18 September 2026: `sleep_page_run` restores the pkexec + helper prefix (sleep-mask and lid actions reach the privileged helper again); status complete-statistics gains the missing `"stats"` mode argument; `check-helper-boundary.sh` extended to cover `sleep-page.c`/`status-page.c`. Backend 6/6 + boundary audit pass.

## luxerandr 0.3.2+gtk4-2+luxe4 (supersedes luxe3; superseded by 0.3.2+gtk4-2+luxe5)

- 18 September 2026: `on_enable_toggled()` shadowing fixed — enabled monitors are now counted per-element instead of reusing the outer variable (`-Wshadow` clean); compiled-in version string now carries the full `0.3.2+gtk4-2+luxe4` suffix. 6/6 CTests pass.

## luxeterm 0.3.2+gtk4-2+luxe3 (supersedes luxe2; superseded by 0.3.2+gtk4-2+luxe4)

- 18 September 2026: tab label created exactly once (orphaned `gtk_label_new()` + dead `user_specified_label` branch removed); new `current_term()` helper guards all seven tab-index handlers against page -1/out-of-range. Shell-free URL launch re-confirmed in the shipped binary.

## 18 September 2026 — hreport P1 repair pass

Eight packages rebuilt for third-audit P1 findings, each version-bumped per the packaging rule, CTest-verified, payload-audited, and collected into `artifacts/`.

## luxedm 1.4.1-1+luxe6 (supersedes luxe5; superseded by 1.4.1-1+luxe7)

- 18 September 2026: `console`/`exit` magic usernames now require real PAM authentication — the "simply fake!" bypass deleted in both PAM and non-PAM builds (the conversation no longer aborts the password prompt for those actions, so deleting only the bypass would have made them un-authenticatable). `console_cmd` compiled-in default emptied; `App::Console()` is a silent no-op on empty command. `luxedm-lock` re-execs before teardown on SIGTERM/INT/HUP/QUIT (grabs never released, display fd CLOEXEC'd) instead of exiting unlocked. New `luxedm-lock-signals` CTest with negative controls; 8/8 pass. Note: shipped `data/luxedm.conf` still carries an explicit xterm `console_cmd` — now an authenticated-only admin choice.

## luxenotify 0.1.0-2 (supersedes 0.1.0-1)

- 18 September 2026: max 32 concurrent live notifications (oldest evicted with reason 3 + `NotificationClosed`; replacements reuse their id and never trigger eviction); max 10 action pairs per Notify (extras ignored); `_NET_CURRENT_DESKTOP` and `_NET_NUMBER_OF_DESKTOPS` treated as untrusted with clamping and overflow-free `desktop >= count / 4` guard. Extended smoke test (excess-actions cap, 40-fill eviction, hostile `0xffffffff` desktop) passes.

## luxesession 0.5.5+gtk4-4+luxe10 (supersedes luxe9)

- 18 September 2026: logout lock moved to validated `$XDG_RUNTIME_DIR` (cache-dir then uid-keyed `/tmp` fallbacks) with `O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC` + fstat uid/type/nlink checks; `unlink()` only when verified. Logout prompt interpolants escaped with `g_markup_escape_text()`; trailing-newline strip guarded against empty output. `DESKTOP_SESSION` clamped in `luxe-migrate-user-config` (rejects empty, `..`, `/`, absolute; falls back to LXDE — 10/10 harness vectors). Migration CTest passes.

## luxepolkit 0.1.0+gtk4-5+luxe6 (supersedes luxe5)

- 18 September 2026: empty identity list now fails closed with `G_IO_ERROR_FAILED` ("No identities available for authentication.") instead of authorizing silently; dialog-lifecycle CTest extended with a live empty-identities case (no dialog left presented). Compiled-in VERSION now carries the full `0.1.0+gtk4-5+luxe6` suffix.

## luxurystore 0.1.0-8 (supersedes 0.1.0-7; superseded by 0.1.0-9)

- 18 September 2026: `popen("lsb_release -c -s")` in the root helper replaced with shell-free `/etc/os-release` `VERSION_CODENAME` parsing (gated off entirely when root); the value feeds only GUI support-time display, never resolve/plan/commit. Backend + workflows tests pass; helper binary confirms zero `lsb_release` references.

## frame 0.1.0-1+luxe4 (supersedes luxe3; superseded by 0.1.0-1+luxe5)

- 18 September 2026: `g_setenv("GSETTINGS_SCHEMA_DIR", ".")` deleted from production `main()`; compiled-in VERSION carries the full suffix. Smoke test passes.

## shotty 0.1.0-1+luxe3 (supersedes luxe2; superseded by 0.1.0-1+luxe4)

- 18 September 2026: same `GSETTINGS_SCHEMA_DIR` deletion; full-suffix compiled-in VERSION. Smoke test passes.

## lucid 0.1.0-1+luxe3 (supersedes luxe2; superseded by 0.1.0-1+luxe4)

- 18 September 2026: all `atoi()`-derived modeline values go through `parse_modeline_uint_clamped()` (`strtol`-based, ranges from the GSettings schema: tab 1-32, margin 1-1000); defensive clamp before view setters. In-range values unchanged; `ts=4000000000` → 32. Clamp harness passes; session-state + editor-integration pass.

## 18 September 2026 — hreport P2 Wave A (structural debt, 7 packages)

## debinstall 0.1.0-3 (supersedes 0.1.0-2; superseded by 0.1.0-4)

- 18 September 2026: `ui_smoke.py` fixed for `Gtk.AlertDialog` and registered as the `debinstall-ui` CTest (Xephyr-guarded; 3/3 pass with backend + boundary); helper path via CMake `configure_file()` constant (`paths.py`, correct across lib/lib64/RPM layouts); `src/__pycache__` purged.

## frame 0.1.0-1+luxe5 (supersedes luxe4; superseded by 0.1.0-1+luxe6)

- 18 September 2026: legacy `Makefile` deleted; unused X11 dependency removed from CMake and sources (only the standalone test helper used X11); `Copyright (C) 2026 Jerry Howell` added to all 6 files. Build warning-free; smoke test passes.

## lucid 0.1.0-1+luxe4 (supersedes luxe3; superseded by 0.1.0-1+luxe5)

- 18 September 2026: copyright headers added (main/state/modeline-parser); `tests/preview.c` wired into the build; full-suffix compiled-in version confirmed. 2/2 CTests pass.

## luxedm 1.4.1-1+luxe7 (supersedes luxe6)

- 18 September 2026: dead ConsoleKit path deleted (7 blocks, `-s` getopt, `Ck` member); 4 unused Find modules + empty cmake dirs deleted; dead knobs (`screenshot_cmd`, `show_welcome_msg`, `tty_lock`) removed from code and shipped config; systemd unit + `/etc` paths via GNUInstallDirs (install paths verified byte-identical to luxe6). 8/8 CTests pass.

## luxeappearance 0.3.0+gtk4-2+luxe3 (supersedes luxe2; superseded by 0.3.0+gtk4-2+luxe4)

- 18 September 2026: `config.h` now generated from CMake (checked-in header + autotools tree + `src/*.o` purged); cursor themes ENABLED instead of deleted (dead branch resolved for real, verified live in Xephyr); CWD `.ui` fallbacks removed; `install-icon-theme.sh` hardened (quoting, `--no-same-owner`, archive-member rejection); dead stubs deleted; 4 copyright lines added. 2/2 CTests pass.

## luxeappearance-obconf 2:0.2.3+gtk4-4+luxe3 (supersedes luxe2; superseded by 2:0.2.3+gtk4-4+luxe4)

- 18 September 2026: autotools tree + checked-in `config.h` (now CMake-generated) + committed `src/obconf` binary purged (22 entries); `gtk_message_dialog_new()` replaced with the AlertDialog pattern (11 deprecation warnings gone, zero new); `_OB_CONFIG_FILE` validated (config-home prefix + regular file, fallback to default); mtime guard hardened (ns + size + inode); dead code deleted; 5 copyright lines added. Workflows CTest passes.

## luxehotkey 0.1.0+gtk4-2+luxe3 (supersedes luxe2; superseded by 0.1.0+gtk4-2+luxe4)

- 18 September 2026: `argv[1]` vs `cmd` dispatch mismatch fixed (`--global`/`--app`/`--show` work as documented); all three FIXMEs closed (key/exec validation, working `show`); rc.xml save now atomic (mkstemp + fsync + rename, symlink-refusing); `c_std_11` on plugin targets; 2 copyright lines added. 3/3 CTests pass.

## luxepolkit 0.1.0+gtk4-5+luxe7 (supersedes luxe6)

- 18 September 2026: dead hidden combo box removed; `GSimpleAsyncResult`+`g_slice` migrated to `GTask`+`g_new0`; 4 stale autotools files deleted; copyright line added. Dialog-lifecycle CTest passes.

## luxeinput 0.3.5-1+luxe3 (superseded by 0.3.5-1+luxe4)

- 18 September 2026: 16-file autotools tree purged; dead headers/stubs removed; `sh -c` autostart writer deleted (session `desktop.conf` path retained); translations ENABLED (63 `.mo` now load); GNUInstallDirs paths. No tests registered.

## luxemenu-data 0.1.4+luxe-2+luxe2 (supersedes luxe1)

- 18 September 2026: dead C headers + 21 `.directory.in` templates + `POTFILES.in` deleted (25 files); install rule hardened with `.directory.in` EXCLUDE. Installed payload file list byte-identical (328 entries).

## luxesession 0.5.5+gtk4-4+luxe11 (supersedes luxe10)

- 19 September 2026: dead autostart code deleted (~2,500 LoC + generated-C mirror cleanup); dead polkit agent data deleted; `#if 0` + GTK2/3 logout branches stripped; ~170 stale artifacts/binaries purged (generated `config.h` replaced with CMake defines). Migration CTest passes.

## luxerandr 0.3.2+gtk4-2+luxe5 (supersedes luxe4; superseded by 0.3.2+gtk4-2+luxe6)

- 19 September 2026: command builder extracted to `command.c`/`command.h`; 6 grep-tests replaced with ONE behavioral `luxerandr-command` CTest (6 cases, 25 checks); ~90 autotools artifacts + committed binary purged; stale `config.h` gone; dead blocks deleted; `output` leak fixed. 1/1 CTest passes.

## luxetask 0.1.9+gtk4-1+luxe4 (supersedes luxe3; superseded by 0.1.9+gtk4-1+luxe5)

- 19 September 2026: compatibility stubs + 5 unused globals + dead declarations deleted; `check_config()` rewritten as a 9-entry table loop; `(gpointer)` cast replaced with a proper wrapper; `save_config()` symlink-safe (mkstemp + fsync + rename); 99 autotools artifacts + binary purged; broken `include/` dir fixed. Warning profile identical to baseline; `pid-protection` still passes.

## luxup 0.1.1-1+luxe2 (new)

- 19 September 2026: process-wide `XSetErrorHandler` replaced with scoped push/pop traps; redundant `(void)` tails cleaned. Snap-policy test passes. (First versioned P2 pass for this package.)

## shotty 0.1.0-1+luxe4 (supersedes luxe3; superseded by 0.1.0-1+luxe5)

- 19 September 2026: stale `Makefile` deleted (smoke script CMake-only); deprecation pragmas balanced (zero warnings, was 2); 3 dead schema keys dropped (strict validation green); `LICENSE`/`COPYING.slop` deduplicated; README rewritten for Luxe; NULL-path guard + 30 s temp-file grace added; 6 copyright headers added. Smoke test passes.

## luxepanel 0.10.1+gtk4-8+luxe19 (supersedes luxe18)

- 19 September 2026: dead plugin trees deleted (netstat with its `system()` call, indicator, pager.c, empty netstatus/weather — all verified unbuilt/unreferenced); ~700 KB autotools residue + 2127-line GTK2 `.glade` + byte-duplicate `.ui` + `.pc` files purged; 5 priority copyright lines added (39 remain); `c_std_11` on all plugin targets. 4/4 CTests pass.

## luxurystore 0.1.0-9 (supersedes 0.1.0-8; superseded by 0.1.0-10)

- 19 September 2026: ~23 MB unused vendored/staged material pruned (unused frontend, docs, tarball, downloads; `.deps` apt headers kept — consumed by the build); 5 hardcoded helper paths replaced with one `LUXURYSTORE_HELPER_PATH` constant; dead `Volatile::filterFile` reference removed; copyright headers added. Backend + workflows pass.

## pcmanager 1.4.0+gtk4-4+luxe7 (supersedes luxe6; superseded by 1.4.0+gtk4-4+luxe8)

- 19 September 2026: 15,137 dead LoC deleted (11 GTK3-era sources + 9 headers); 244 KB `.glade` + unused `.ui` install removed; committed ELFs/`*.o`/autotools purged; `pcmanager.desktop` now CMake-generated with msgfmt translation merge (validates clean); Properties cancellable wired into `g_task_new`; `menu-cache-gen`/`menu-cached` excluded from build; copyright line added. Vendored-tree deletion deferred (breaks 3-package libfm-sync). 6/6 CTests pass.

## frame 0.1.0-1+luxe6 (supersedes luxe5)

- 19 September 2026 (Adw): `AdwApplication`/`AdwApplicationWindow` + `adw_init()`; `.ui` titlebar reworked to `AdwToolbarView`; `AdwAboutDialog`; FileChooser initial folder set. Fixed en route: fit-zoom startup race and fullscreen NULL-titlebar handling. Smoke passes all 7 phases; DEB carries `libadwaita-1-0`.

## shotty 0.1.0-1+luxe5 (supersedes luxe4)

- 19 September 2026 (Adw pilot): `AdwApplication`/`AdwApplicationWindow` + `adw_init()` with GUI-target-only libadwaita link; `.ui` titlebar reworked to `AdwToolbarView` (same header bar); `AdwAboutDialog` with identical content; FileChooser initial-folder regression fixed (portal backend). Smoke passes all phases; DEB carries the `libadwaita-1-0` dependency. Pattern notes for the remaining 12 apps: no `<requires lib="libadwaita">` in `.ui`; always set FileChooser initial folder; grep all `SMOKE-*-OK` lines.

## lucid 0.1.0-1+luxe5 (supersedes luxe4)

- 19 September 2026 (Adw): `AdwApplication` + header-bar/hamburger UI replacing menu bar + toolbar (all 31 actions kept); `AdwAboutDialog`; FileChooser initial folders set; dark-theme via `AdwStyleManager`. Session-state + editor-integration pass; DEB carries `libadwaita-1-0`.

## luxerandr 0.3.2+gtk4-2+luxe6 (supersedes luxe5)

- 19 September 2026 (Adw): `AdwApplication`/`AdwApplicationWindow` + `adw_init()`; `AdwToolbarView` header in code; hamburger menu added; `AdwAboutDialog`; command builder stays GTK-free. Behavioral `luxerandr-command` CTest passes unchanged; DEB carries `libadwaita-1-0`.

## luxetask 0.1.9+gtk4-1+luxe5 (supersedes luxe4)

- 19 September 2026 (Adw): `AdwApplication`/`AdwApplicationWindow` + `adw_init()`; header bar hosted in `AdwToolbarView`; bible 10/6 spacing applied; `AdwAboutDialog`. `destructive-action` on popover menu items noted as a genuine limitation (menu models can't carry CSS classes). `pid-protection` passes; DEB carries `libadwaita-1-0`.

## luxeinput 0.3.5-1+luxe4 (supersedes luxe3)

- 19 September 2026 (Adw): `AdwApplication`/`AdwApplicationWindow` + `adw_init()`; OK/Cancel moved into header bar (OK `suggested-action`); 12→10px margins; `AdwAboutDialog` (license escaped for Pango); German translations spot-check green. Xephyr clean; DEB carries `libadwaita-1-0`.

## luxeappearance 0.3.0+gtk4-2+luxe4 (supersedes luxe3)

- 19 September 2026 (Adw): `AdwApplication`/`AdwApplicationWindow`; `.ui` titlebar → `AdwToolbarView` + hamburger (Apply `suggested-action`, Remove `destructive-action`); `AdwAboutDialog`; FileChooser initial folder set. 2/2 CTests pass; DEB carries `libadwaita-1-0`.

## luxeappearance-obconf 2:0.2.3+gtk4-4+luxe4 (supersedes luxe3)

- 19 September 2026 (Adw): `AdwApplication`/`AdwApplicationWindow` in code; header bar (Apply `suggested-action`); hamburger + `AdwAboutDialog`; archive installer FileDialogs with initial folders. Workflows CTest passes; Xephyr clean; DEB carries `libadwaita-1-0`.

## luxeterm 0.3.2+gtk4-2+luxe4 (supersedes luxe3)

- 19 September 2026 (Adw): `AdwApplication`/`AdwApplicationWindow` + `adw_init()`; menu bar → header bar + hamburger (all actions kept); `AdwAboutDialog`. Live Xephyr check: window maps with correct shell title, zero Gtk-CRITICALs; shell-free URL path re-confirmed. DEB carries `libadwaita-1-0`.

## debinstall 0.1.0-4 (supersedes 0.1.0-3)

- 19 September 2026 (Adw): `Adw.Application`/`Adw.ApplicationWindow`; header bar in `AdwToolbarView`; 20→10px margins; modal `Adw.AboutDialog` with website added; `gir1.2-adw-1` dependency declared. 3/3 CTests pass; Xephyr launch clean.

## luxehotkey 0.1.0+gtk4-2+luxe4 (supersedes luxe3)

- 19 September 2026 (Adw): GUI plugin converted to proper `AdwApplication` (private `GMainLoop` removed); window in `AdwToolbarView`; Save `suggested-action` / Delete `destructive-action`; `AdwAboutDialog`. 3/3 CTests pass; Xephyr clean under fatal-warnings; live rc.xml untouched. DEB carries `libadwaita-1-0`.

## luxurystore 0.1.0-10 (supersedes 0.1.0-9)

- 19 September 2026 (Adw): `AdwApplication`/`AdwApplicationWindow` + `adw_init()` (helper stays GTK/Adwaita-free, `ldd`-verified); header in `AdwToolbarView`; 10/6/14 spacing; `AdwAboutDialog` with website added; tests updated for non-window dialogs. Backend + workflows pass; Xephyr maps clean; DEB carries `libadwaita-1-0`.

## pcmanager 1.4.0+gtk4-4+luxe8 (supersedes luxe7)

- 19 September 2026 (Adw, browser only; wallpaper desktop exempt and untouched): `AdwApplication` (replaces private `GMainLoop`) + `AdwApplicationWindow`; toolbar in `AdwToolbarView` with 10/6 spacing; Empty Trash `destructive-action`; name-prompt dialog de-deprecated; `GtkAppChooser` pragma-suppressed (no replacement exists); `AdwAboutDialog`. Warnings 33→19, zero new; 6/6 CTests pass; DEB carries `libadwaita-1-0`.

## Timeline summary

1. **Pre-12 September 2026** — original ports and creations: all 22 packages built, branded, and packaged (per-package first entries above); 8 September 2026 distribution-plan confirmations (TLP set, tray plugin).
2. **12 September 2026** — catalog documented (`luxenew.md`, `LUXE-BIBLE.md`), 22 DEBs collected.
3. **13 September 2026** — grade report (`greport.md`) and reality assessment (`GREPORT-ASSESSMENT-PLAN.md`); remediation Steps 1–18 executed: artifact safety (Step 1), per-package stabilization (Steps 2–16), suite-wide rebuild and validation gates (Step 17), session WM-spawn dogfooding fix (Step 18). P0 and P1 application-stabilization complete.
4. **Outstanding** — user re-login confirmation (Step 19), CI ctest jobs, VM acceptance run, bible/credits/version reconciliation, then P2 (libadwaita migration pass, measured optimization, provenance list, source cleanup).
5. **14 September 2026** — `luxepower` created and `tlpui` removed from the package set; `LUXE-BIBLE.md`, `DISTRO.md`, and the component list updated. The artifact collector now expects 23 packages.
6. **16 September 2026** — `luxeterm` P0 fix: shell-free URL launch (`terminal_open_url()`) plus Pango-markup escaping of terminal titles; `arecord.md` cross-cutting item 1 closed.
7. **16 September 2026** — full `arecord.md` remediation pass: every per-package finding fixed and build/test-verified (see per-package entries above); cross-cutting closed — `internal/libfm` hash-sync enforced (`cmake/CheckLibfmSync.cmake` + `libfm-sync` CTest in all three carriers; `fm-module.c` reconciled to the tolerate-absent-dirs version), provenance line added to 56 touched files, validation/RPM wiring verified present, shared-include hoist deferred with reason. Deferred residuals recorded per package (libXrandr port, single X connection, store paging, true incremental lists, settings-daemon ownership, RandR hotplug).
7. **16 September 2026** — full-suite rebuild verification (post-remediation): all 22 packages configured, compiled (`Unix Makefiles`, `Release`, `/usr` prefix, `BUILD_TESTING=OFF`) and packaged to fresh DEBs in one session; all 22 passed the shared `AuditDeb.py` payload audit (no set-ID payloads, no group-writable dirs). Shipped-binary check confirms the `luxeterm` fix (`xdg-open %s` format string gone; `g_app_info_launch_default_for_uri` + argv `xdg-open` fallback present). Note: this container lacked several `-dev` headers, satisfied via `apt-get download` extraction to a staging dir only (no system install); `luxeappearance-obconf` links against the shared `libobrender`/`libobt` (the staged static archives were dropped to match dev-machine behavior). Staging DEBs were then promoted with the audited `CollectArtifacts` run on
the dev machine (22/22 payload audits at collection, `manifest.json` +
`debs/SHA256SUMS` rewritten, `UNRELEASED.md` updated).
## 29 October 2026 — LuxuryStore deterministic APT tests

Run both GUI backend tests against a private local APT repository with available, installed, dependency, removal and upgrade cases instead of the CI runner's full package database. A separate uninstalled non-root fixture helper uses that database for simulation; the shipped helper retains its APT configuration sanitization and is still checked for authorization and invalid requests. Both APT ABIs and the Flatpak tests pass locally. GitHub Ubuntu and Debian jobs still need rerunning.

## 29 October 2026 — Restore panel volume and session services

LuxePanel luxe65 and LuxeSession luxe51 recover a missing XDG_RUNTIME_DIR only when the existing /run/user directory belongs to the user and is private; an explicitly supplied runtime directory is preserved. Panel can reconnect its volume control, and newly started desktop applications can reach the user session bus and sound server. Runtime-path safety tests, an isolated session child test, the panel's nested-X tray embedding test and a real read-only mixer probe passed. Reinstall both packages and log out and back in to restart the desktop apps; tray icons from individual applications still need confirmation in the new session.
