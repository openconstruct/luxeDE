#!/usr/bin/python3 -I
# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
"""Polkit entry point. Only the installation worker runs as root."""
import os
from pathlib import Path
import re
import signal
import sys
import tempfile
installed_modules = '@CMAKE_INSTALL_FULL_LIBDIR@/debinstall'
if Path(installed_modules).is_dir():
    sys.path.insert(0, installed_modules)
else:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
from snapshot import copy_package

def copy_verified(source, destination, uid, expected):
    return copy_package(source, destination, expected_owner=uid, expected_sha256=expected)


def copy_with_deadline(source, destination, uid, expected, timeout=120):
    """Interrupt a privileged snapshot read that exceeds its time budget."""
    def expired(_signal_number, _frame):
        raise TimeoutError('Package snapshot copy exceeded its 120-second deadline.')

    previous_handler = signal.signal(signal.SIGALRM, expired)
    previous_timer = signal.setitimer(signal.ITIMER_REAL, timeout)
    try:
        return copy_verified(source, destination, uid, expected)
    finally:
        signal.setitimer(signal.ITIMER_REAL, *previous_timer)
        signal.signal(signal.SIGALRM, previous_handler)

def main():
    if os.geteuid() != 0:
        raise PermissionError('Start installation from DebInstall to authenticate.')
    if len(sys.argv) != 4:
        raise ValueError('Expected a package snapshot, checksum and reviewed plan.')
    uid = int(os.environ.get('PKEXEC_UID', '-1'))
    if uid <= 0:
        raise PermissionError('A requesting desktop user is required.')
    source, expected, token = sys.argv[1:]
    if not re.fullmatch(r'[0-9a-f]{64}', token):
        raise ValueError('Invalid installation plan.')
    # Fixed process environment: no caller-controlled APT, Python or loader settings.
    os.environ.clear()
    os.environ.update(PATH='/usr/sbin:/usr/bin:/sbin:/bin', HOME='/root', LANG='C.UTF-8',
                      TERM='xterm-256color', PYTHONUNBUFFERED='1')
    from backend import inspect_package
    with tempfile.TemporaryDirectory(prefix='debinstall-', dir='/var/tmp') as directory:
        target = str(Path(directory) / 'package.deb')
        copy_with_deadline(source, target, uid, expected)
        data, gdebi = inspect_package(target)
        if not data['ready']:
            raise ValueError(data['failure'])
        if data['plan_token'] != token:
            raise ValueError('Package state or dependencies changed. Reopen the package to review the new plan.')
        print(f"Installing {data['package']} {data['version']} with gdebi-core…", flush=True)
        result = gdebi.install()
        if result != 0:
            raise RuntimeError(f'Installation did not finish (exit status {result}). See the output above.')
        print('\nInstallation completed successfully.', flush=True)

if __name__ == '__main__':
    try:
        main()
    except (Exception, KeyboardInterrupt) as error:
        print(f'\nDebInstall: {error}', file=sys.stderr, flush=True)
        sys.exit(1)
