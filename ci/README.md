# Independent package builds

See [pre-upload validation and limits](VALIDATION.md) for the checks performed.

The monorepo contains all 25 package sources. `cmake/LuxeComponents.cmake` is
the authoritative package list. `.github/workflows/packages.yml` creates one
job per package **and target distribution**: Ubuntu 24.04 and Debian 13 amd64.
The runner is Ubuntu 24.04; compilation happens inside the target's container.
Use the DEBs for your distribution, especially for versioned media libraries.
LuxuryStore continues to contain its APT 6/7 dispatch backends.

Pushes and pull requests build the full 50-job matrix (four concurrent jobs).
Manual dispatch accepts one package directory name or `all`. Jobs compile as
an ordinary user and run behavioral tests under a private Xvfb/D-Bus session.
Tests needing a sibling application installed outside their fixture can remain
conditional; CTest XML records skipped tests. A passing build is not physical
hardware or first-login acceptance.

Each job uploads its DEB, checksums, source revision, build environment and test
diagnostics. Full-suite runs also verify package ownership and simulate APT
installation of each 25-package cohort. They do not install on your machine,
publish a release, commit output, or rotate the local frozen rollback set.

Artifact filenames escape Debian epoch colons as `%3A` before checksums are
generated; package metadata keeps its original epoch. The Panel job builds as
the ordinary user, installs that build into the disposable job container as
root, and runs tests and packaging as the ordinary user. Its volume fixture
checks the installed plugin matches the build and does not require a nested
Bubblewrap namespace in this mode.

## Reproduce

In a disposable target-distribution container, as root:

```sh
LUXE_CI_CONTAINER=1 bash ci/install-dependencies.sh
useradd --create-home --shell /bin/bash luxe-build
chown -R luxe-build:luxe-build /work
```

For LuxeSettings, set `LUXE_CI_COMPONENT=luxesettings` when installing
dependencies; this also generates the German locale used by its UI test.
For LuxuryStore, set `LUXE_CI_COMPONENT=luxurystore` to provide the state
directory its package normally creates before the unprivileged tests run.

Then from `/work`, as that ordinary user:

```sh
bash ci/build-package.sh lucid ubuntu-24.04
```

Use `debian-13` in a Debian 13 container. The label alone does not change the
host libraries. Output lives in `build/ci/<distribution>/<component>/`.
Containers need nested user namespaces for isolated GUI tests; the workflow
relaxes container seccomp/AppArmor for this purpose, not the host desktop.
Only checked-out code and disposable build dependencies belong in those jobs.
The GitHub Panel job uses the container installation described above; the
single-command local Panel workflow instead stages helpers and plugins in a
private mount namespace. The volume test starts its own null-sink audio server;
it neither needs a sound card nor connects to the user's server.

LuxuryStore fetches SHA256-pinned Ubuntu/Debian APT headers and runtime
libraries with `ci/AptPrefixes.cmake`, extracting them under `build/ci/`.
Neither foreign runtime is installed on the build host or bundled in the DEB.
For a manual CMake build, pass the extracted `apt6` and `apt7` directories as
`LUXURYSTORE_APT6_ROOT` and `LUXURYSTORE_APT7_ROOT`.

## Source hygiene

Run `bash ci/check-source.sh` before uploading.
Package directories hold source, tests, assets, licenses and CMake definitions,
not build trees, downloaded development prefixes or compiled output. Maintained
generated C and hash-pinned icon source archives are intentional source inputs.
The historical Settings comparison lives in `tools/diagnostics/`, outside the
shipping Settings package. No application features are removed for cleanliness.

Do not commit `build/`, generated DEBs, diagnostic screenshots or machine-local
artifact manifests. Keep the current and frozen DEBs locally while testing.

## Native tooling

Source hygiene uses `ci/check-source.sh`. DEB checks and artifact organization
use C++17/libarchive through the `cmake/*.sh` entry points; downloaded cohorts
use `ci/CollectDownloads.cmake`. See `tools/native/README.md`. The cohort audit
container needs CMake, a C++ compiler, Make and libarchive development headers.
Python packages in the full build/test container support retained application
runtime code and DebInstall tests. Other build, diagnostic and test tooling uses
C++17, CMake or shell; Python is not part of the packaging pipeline.
