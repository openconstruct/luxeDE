# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
import sys
import threading
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'src'))
from process_runner import ProcessOutputLimitError, run_bounded


class ProcessRunnerTests(unittest.TestCase):
    def test_captures_both_streams_within_limit(self):
        result = run_bounded(
            [sys.executable, '-c', "import sys; print('out'); print('err', file=sys.stderr)"],
            timeout=2,
            max_output_bytes=64,
        )
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout, 'out\n')
        self.assertEqual(result.stderr, 'err\n')

    def test_terminates_when_output_limit_is_exceeded(self):
        with self.assertRaises(ProcessOutputLimitError):
            run_bounded(
                [sys.executable, '-c', "import os; os.write(1, b'x' * 1000000)"],
                timeout=5,
                max_output_bytes=1024,
            )

    def test_terminates_on_deadline(self):
        started = time.monotonic()
        with self.assertRaises(TimeoutError):
            run_bounded(
                [sys.executable, '-c', 'import time; time.sleep(10)'],
                timeout=0.1,
                max_output_bytes=1024,
            )
        self.assertLess(time.monotonic() - started, 3)

    def test_cancels_child(self):
        cancelled = threading.Event()
        timer = threading.Timer(0.1, cancelled.set)
        timer.start()
        try:
            with self.assertRaisesRegex(RuntimeError, 'cancelled'):
                run_bounded(
                    [sys.executable, '-c', 'import time; time.sleep(10)'],
                    timeout=5,
                    max_output_bytes=1024,
                    cancel_event=cancelled,
                )
        finally:
            timer.cancel()


if __name__ == '__main__':
    unittest.main()
