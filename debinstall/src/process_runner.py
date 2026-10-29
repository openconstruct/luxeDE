# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
"""Run helper processes with bounded output, deadlines, and cancellation."""
import os
import selectors
import signal
import subprocess
import time


class ProcessOutputLimitError(RuntimeError):
    """Raised when a child produces more output than the caller permits."""


def _stop_process_group(process):
    process.poll()  # Reap an already-exited leader before checking its group.
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    deadline = time.monotonic() + 2
    while time.monotonic() < deadline:
        try:
            os.killpg(process.pid, 0)
        except ProcessLookupError:
            break
        time.sleep(0.05)
    else:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    process.wait()


def run_bounded(args, timeout, max_output_bytes, cancel_event=None):
    """Run a process, retaining at most `max_output_bytes` from both streams."""
    if cancel_event is not None and cancel_event.is_set():
        raise RuntimeError('Process was cancelled.')

    process = subprocess.Popen(
        args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True
    )
    buffers = {process.stdout: bytearray(), process.stderr: bytearray()}
    selector = selectors.DefaultSelector()
    for stream in buffers:
        os.set_blocking(stream.fileno(), False)
        selector.register(stream, selectors.EVENT_READ)

    deadline = time.monotonic() + timeout
    total = 0
    try:
        while selector.get_map() or process.poll() is None:
            if cancel_event is not None and cancel_event.is_set():
                raise RuntimeError('Process was cancelled.')
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f'Process exceeded its {timeout}-second deadline.')

            for key, _ in selector.select(min(0.1, remaining)):
                stream = key.fileobj
                try:
                    chunk = os.read(stream.fileno(), 65536)
                except BlockingIOError:
                    continue
                if not chunk:
                    selector.unregister(stream)
                    stream.close()
                    continue
                total += len(chunk)
                if total > max_output_bytes:
                    raise ProcessOutputLimitError(
                        f'Process output exceeded the {max_output_bytes}-byte limit.'
                    )
                buffers[stream].extend(chunk)

        return subprocess.CompletedProcess(
            args,
            process.wait(),
            buffers[process.stdout].decode('utf-8', errors='replace'),
            buffers[process.stderr].decode('utf-8', errors='replace'),
        )
    except BaseException:
        _stop_process_group(process)
        raise
    finally:
        selector.close()
        for stream in buffers:
            if not stream.closed:
                stream.close()
