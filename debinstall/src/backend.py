# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
"""Read-only gdebi-core planning, shared by the UI worker and root helper."""
import hashlib
import json
import os
from pathlib import Path
import sys
from types import SimpleNamespace
sys.path.insert(0, str(Path(__file__).resolve().parent))
from process_runner import run_bounded

CORE = Path('/usr/share/gdebi')
if not CORE.is_dir() and os.geteuid() != 0:
    CORE = Path(__file__).resolve().parents[1] / 'build/deps/root/usr/share/gdebi'
sys.path.insert(0, str(CORE))

MAX_FILES = 2000
MAX_LISTING_OUTPUT_BYTES = 16 * 1024 * 1024


class GDebiAdapter:
    """Keep gdebi-core's private compatibility surface in one place."""

    def __init__(self, application):
        self._application = application

    @property
    def cache(self):
        return self._application._cache

    def bind_package(self, package):
        self._application._deb = package

    @staticmethod
    def fields(package):
        return package._sections

    @staticmethod
    def failure(package):
        return package._failure_string

    def install(self):
        return self._application.install()


def list_package_files(path):
    """Return a bounded display list without materializing GDebi's full list."""
    result = run_bounded(
        ['/usr/bin/dpkg-deb', '--contents', str(path)],
        timeout=60,
        max_output_bytes=MAX_LISTING_OUTPUT_BYTES,
    )
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or 'Could not read package contents.')
    files = []
    count = 0
    for line in result.stdout.splitlines():
        fields = line.split(None, 5)
        if count < MAX_FILES:
            files.append(fields[5] if len(fields) == 6 else line)
        count += 1
    return files, max(0, count - MAX_FILES)


def digest_file(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def inspect_package(path):
    from GDebi.GDebiCli import GDebiCli
    from GDebi.DebPackage import DebPackage
    # None selects the live system cache. Passing '/' activates python-apt's
    # alternate-root setup, which tries to create absent files such as the
    # optional legacy /etc/apt/sources.list, even for a read-only inspection.
    options = SimpleNamespace(quiet=True, rootdir=None, apt_opts=[], non_interactive=True)
    app = GDebiCli(options)
    gdebi = GDebiAdapter(app)
    was_broken = gdebi.cache.broken_count > 0
    package = DebPackage(str(path), gdebi.cache)
    gdebi.bind_package(package)
    ready = package.check()
    install, remove, untrusted = package.required_changes
    fields = gdebi.fields(package)
    changes = []
    for name in install:
        item = gdebi.cache[name]
        changes.append({
            'action': 'Upgrade' if item.installed else 'Install',
            'name': name,
            'version': item.candidate.version if item.candidate else '',
        })
    for name in remove:
        item = gdebi.cache[name]
        changes.append({
            'action': 'Remove',
            'name': name,
            'version': item.installed.version if item.installed else '',
        })
    changes.sort(key=lambda row: (row['action'], row['name']))
    installed = None
    if package.pkgname in gdebi.cache:
        installed = gdebi.cache[package.pkgname].installed
    failure = gdebi.failure(package) if not ready else ''
    if was_broken:
        ready = False
        failure = 'The system has broken dependencies. Repair them before installing this package.'
    elif untrusted:
        ready = False
        failure = 'Dependencies from unauthenticated repositories: ' + ', '.join(untrusted)
    elif any(gdebi.cache[name].essential for name in remove):
        ready = False
        failure = 'This package would remove an essential system package.'
    display_fields = (
        'Package', 'Version', 'Architecture', 'Maintainer', 'Description',
        'Depends', 'Pre-Depends', 'Homepage', 'Installed-Size',
    )
    data = {key.lower(): fields.get(key, '') for key in display_fields}
    data.update(
        ready=bool(ready),
        failure=failure,
        changes=changes,
        installed=installed.version if installed else '',
        sha256=digest_file(path),
    )
    # Include installed state and dependency versions so changes require a fresh review.
    approved_fields = ('package', 'version', 'architecture', 'installed', 'changes', 'sha256')
    approved = {key: data[key] for key in approved_fields}
    data['plan_token'] = hashlib.sha256(json.dumps(approved, sort_keys=True).encode()).hexdigest()
    files, hidden = list_package_files(path)
    data['files'] = files
    data['files_truncated'] = hidden
    return data, gdebi


if __name__ == '__main__':
    try:
        if len(sys.argv) != 2:
            raise ValueError('Expected one package snapshot path.')
        data, _adapter = inspect_package(sys.argv[1])
        print(json.dumps(data))
    except Exception as error:
        print(json.dumps({'error': str(error)}))
        sys.exit(1)
