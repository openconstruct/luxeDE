# Pre-upload validation — 9 October 2026

## Ubuntu 24.04 amd64

All 24 components compiled and produced audited DEBs with native Noble
libraries in an isolated Ubuntu filesystem. **112 CTests passed, zero failed,
zero skipped.** Menu Data is data-only, with no CTests. This does not replace
hardware or first-login acceptance.

The complete cohort passes checksums and cross-package file ownership checks.
APT simulation resolves all 24 packages using Noble repositories and an empty
installed-package status. No applications were actually installed. That check
caught LuxeDM's Debian-only JPEG dependency, now generated from the target's
actual ELF dependencies.

Ignored local evidence: `build/ci/ubuntu-24.04/*/ctest.xml`, `build/ci/logs/`,
`build/ci/noble-cohort-final/`, and `build/ci/noble-apt-final.log`.

The local rootless filesystem cannot apply a few system-service ownership
changes during dependency configuration. GUI, Trash, audio and authentication
fixtures nevertheless ran unprivileged with private displays/buses; no host
services were installed. GitHub installs dependencies as container root.

## Debian 13 amd64

Four compatibility candidates build natively and pass DEB payload audits:
Lucid luxe12, LuxUp luxe13, Panel luxe43 and LuxeDM luxe13. Lucid's two tests,
LuxUp's three and LuxeDM's eleven pass. Panel's private-audio test requires
PulseAudio, absent on this host; it passes on Ubuntu and is included in CI.
The remaining 18 Panel tests pass using private X11 fixtures (34 passing tests
across these four Debian candidates). Do not claim Debian audio
acceptance or a complete Debian-container run from these checks.

The full 24-package Debian container matrix still needs its first GitHub run.
Use distribution-matched DEBs: an Ubuntu build does not guarantee Debian has
its JPEG/FFmpeg and other ABI requirements.

## Repository and workflow

- All 24 package directories pass source hygiene, including symlink and ELF checks.
- Matrix covers 24 components and two distributions; single selection works and invalid selection fails.
- Workflow passes actionlint 1.7.12; shell/Python syntax checks pass.
- Complete-cohort collection, checksums and ownership auditing ran locally.
- Current and frozen rollback DEB checksums remain unchanged.
- Local `main` is initialized. No commit, remote, upload or GitHub run exists yet.
- Builds fetch verified APT prefixes; no untracked package-local dependency is required.

The workflow builds artifacts; it does not publish releases or rotate rollback DEBs.
