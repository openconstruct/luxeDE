# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
"""Bounded copies of untrusted local Debian package snapshots."""
import hashlib
import os
import stat
from pathlib import Path

MAX_PACKAGE_BYTES = 2 * 1024 * 1024 * 1024
_CHUNK_BYTES = 1024 * 1024


def copy_package(source, destination, expected_owner=None, expected_sha256=None,
                 cancel_event=None):
    """Copy one stable regular package, rejecting size changes and excess bytes."""
    if expected_sha256 is not None and (
            len(expected_sha256) != 64 or
            any(char not in '0123456789abcdef' for char in expected_sha256)):
        raise ValueError('Invalid package checksum.')

    fd = os.open(source, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        incoming = os.fdopen(fd, 'rb')
        fd = -1  # Ownership transfers to the file object.
        with incoming:
            before = os.fstat(incoming.fileno())
            if not stat.S_ISREG(before.st_mode):
                raise ValueError('Choose a regular .deb file.')
            if expected_owner is not None and before.st_uid != expected_owner:
                raise ValueError('The package snapshot must be owned by the requesting user.')
            if before.st_size > MAX_PACKAGE_BYTES:
                raise ValueError('Package exceeds the 2 GiB inspection limit.')

            digest = hashlib.sha256()
            written = 0
            try:
                with open(destination, 'xb') as outgoing:
                    while written < before.st_size:
                        if cancel_event is not None and cancel_event.is_set():
                            raise ValueError('Package copy was cancelled.')
                        block = incoming.read(min(_CHUNK_BYTES, before.st_size - written))
                        if not block:
                            raise ValueError('Package changed while it was being copied.')
                        outgoing.write(block)
                        digest.update(block)
                        written += len(block)
                    if incoming.read(1):
                        raise ValueError('Package grew while it was being copied.')
                    if cancel_event is not None and cancel_event.is_set():
                        raise ValueError('Package copy was cancelled.')
                    after = os.fstat(incoming.fileno())
                    if ((before.st_dev, before.st_ino, before.st_size,
                         before.st_mtime_ns, before.st_ctime_ns) !=
                            (after.st_dev, after.st_ino, after.st_size,
                             after.st_mtime_ns, after.st_ctime_ns)):
                        raise ValueError('Package changed while it was being copied.')
                actual = digest.hexdigest()
                if expected_sha256 is not None and actual != expected_sha256:
                    raise ValueError('The package changed after review. Open it again before installing.')
                return actual
            except Exception:
                Path(destination).unlink(missing_ok=True)
                raise
    except Exception:
        if fd >= 0:
            os.close(fd)
        raise
