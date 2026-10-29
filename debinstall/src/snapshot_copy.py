# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
"""Isolate snapshot copying so GUI cancellation can stop a blocked read."""
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from process_runner import run_bounded

MAX_COPY_SECONDS = 120
MAX_COPY_OUTPUT_BYTES = 4096


def copy_snapshot(source, destination, cancel_event):
    """Copy a selected package in a killable process and return its digest."""
    worker = Path(__file__).resolve()
    args = ['/usr/bin/python3', '-I', str(worker), str(source), str(destination)]
    try:
        result = run_bounded(
            args,
            timeout=MAX_COPY_SECONDS,
            max_output_bytes=MAX_COPY_OUTPUT_BYTES,
            cancel_event=cancel_event,
        )
        if result.returncode != 0:
            raise RuntimeError(result.stderr.strip() or 'Could not copy the package snapshot.')
        digest = result.stdout.strip()
        if len(digest) != 64 or any(char not in '0123456789abcdef' for char in digest):
            raise RuntimeError('Snapshot copier returned an invalid checksum.')
        return digest
    except BaseException:
        Path(destination).unlink(missing_ok=True)
        raise


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print('Usage: copy_snapshot_worker.py SOURCE DESTINATION', file=sys.stderr)
        raise SystemExit(2)
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    try:
        from snapshot import copy_package

        print(copy_package(sys.argv[1], sys.argv[2]))
    except Exception as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
